/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "app_mibao_files.h"
#include <apps/common/common.h>
#include <apps/common/toast/toast.h>
#include <assets/assets.h>
#include <board.h>
#include <audio/audio_codec.h>
#include <hal/board/hal_bridge.h>
#include <hal/mibao_config.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// 16px 中文字体（与会议/物联网/设置一致，含米宝产品所需全部中文）
LV_FONT_DECLARE(mibao_zh_font_16);

using namespace mooncake;

namespace {

constexpr std::uint32_t kAccentColor     = 0x2DBE8D;
constexpr std::uint32_t kAccentDarkColor = 0x155D4A;
constexpr std::uint32_t kBgColor         = 0x101417;
constexpr std::uint32_t kPanelSoftColor  = 0xF7EFE3;
constexpr std::uint32_t kInkColor        = 0x273238;
constexpr std::uint32_t kDangerColor     = 0xE64B4B;
constexpr std::uint32_t kListBg          = 0xFFFFFF;
constexpr std::uint32_t kListSelBg       = 0x2DBE8D;
constexpr std::uint32_t kListSelText     = 0xFFFFFF;

constexpr int kSampleRateRecord = 16000;  // 录音采样率（与 wav_writer.h 一致）
constexpr int kSampleRateOut    = 24000;  // 喇叭输出采样率（AUDIO_OUTPUT_SAMPLE_RATE）
// 16k → 24k 是 2:3 升采样，每个输入样本对应 1.5 个输出样本。
// 实际做法：交错输出 2 个样本、3 个样本，控制滑动平均，避免重复/丢失。
constexpr const char* kRecordingDirs[] = {"/sdcard/meetings", "/sdcard/personal"};

class SdCardAccessGuard {
public:
    SdCardAccessGuard() { hal_bridge::board_begin_sdcard_access(); }
    ~SdCardAccessGuard() { hal_bridge::board_end_sdcard_access(); }
};

void set_label(lv_obj_t* label, const std::string& text) {
    if (label != nullptr) {
        lv_label_set_text(label, text.c_str());
    }
}

lv_obj_t* create_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                       std::uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

lv_obj_t* create_button(lv_obj_t* parent, const char* text, std::uint32_t color,
                        const lv_font_t* font) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 76, 36);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    return button;
}

// 简短 16k→24k 升采样：输入 2 样本输出 3 样本
// 输入 2 个 int16，输出 3 个 int16
inline void upsample_2to3(const int16_t* in, int16_t* out) {
    // 直接线性插值：[0,1,2] =>  in[0], (2*in[0]+in[1])/3, (in[0]+2*in[1])/3, in[1]
    // 总共 2 个输入样本 → 3 个输出样本
    out[0] = in[0];
    out[1] = static_cast<int16_t>((2 * static_cast<int32_t>(in[0]) + static_cast<int32_t>(in[1])) / 3);
    out[2] = static_cast<int16_t>((static_cast<int32_t>(in[0]) + 2 * static_cast<int32_t>(in[1])) / 3);
}

// 解析 WAV 文件的 fmt 块，确认是 PCM 16bit，并返回 sample rate
// 返回 false 表示文件不是有效 WAV
bool parse_wav_header(FILE* f, uint32_t& sample_rate, uint16_t& channels, uint16_t& bits_per_sample,
                      uint64_t& data_offset, uint64_t& data_bytes) {
    uint8_t header[44] = {0};
    if (std::fread(header, 1, 12, f) != 12) return false;
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) return false;

    // 找到 fmt 块
    uint64_t pos = 12;
    while (pos < 256) {  // 通常不会超过 256 字节
        uint8_t chunk_head[8] = {0};
        if (std::fseek(f, static_cast<long>(pos), SEEK_SET) != 0) return false;
        if (std::fread(chunk_head, 1, 8, f) != 8) return false;
        const uint32_t chunk_size = chunk_head[4] | (chunk_head[5] << 8) |
                                     (chunk_head[6] << 16) | (chunk_head[7] << 24);
        if (std::memcmp(chunk_head, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {0};
            if (std::fread(fmt, 1, 16, f) != 16) return false;
            const uint16_t audio_format = fmt[0] | (fmt[1] << 8);
            channels = fmt[2] | (fmt[3] << 8);
            sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits_per_sample = fmt[14] | (fmt[15] << 8);
            if (audio_format != 1 || bits_per_sample != 16) return false;  // 仅支持 PCM 16bit
            pos += 8 + chunk_size;
            break;
        }
        pos += 8 + chunk_size;
    }

    // 找到 data 块
    while (pos < 4096) {
        uint8_t chunk_head[8] = {0};
        if (std::fseek(f, static_cast<long>(pos), SEEK_SET) != 0) return false;
        if (std::fread(chunk_head, 1, 8, f) != 8) return false;
        const uint32_t chunk_size = chunk_head[4] | (chunk_head[5] << 8) |
                                     (chunk_head[6] << 16) | (chunk_head[7] << 24);
        if (std::memcmp(chunk_head, "data", 4) == 0) {
            data_offset = pos + 8;
            data_bytes = chunk_size;
            return true;
        }
        pos += 8 + chunk_size;
    }
    return false;
}

}  // namespace

AppMibaoFiles::AppMibaoFiles() {
    setAppInfo().name = "文件管理";
    // 用 mibao_work_150 作为占位图标（mibao_files 待用户后续提供）
    static auto icon = assets::get_image("mibao_work_150.bin");
    setAppInfo().icon = (void*)&icon;
    static std::uint32_t theme_color = 0x2DBE8D;
    setAppInfo().userData = (void*)&theme_color;
}

void AppMibaoFiles::onCreate() {
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMibaoFiles::onOpen() {
    mclog::tagInfo(getAppInfo().name, "on open");
    _selected_index = -1;
    _playing = false;
    _stop_play = false;
    _rename_old_path.clear();
    _rename_old_name.clear();
    _files.clear();
    _labels.clear();
    _folders.clear();
    _browsing_dir.clear();  // 每次打开都从首页（文件夹列表）开始
    _close_requested = false;

    LvglLockGuard lock;
    createUi();
    view::create_home_indicator([this]() { _close_requested = true; }, kAccentColor, kAccentDarkColor);
    view::create_status_bar(kAccentColor, kAccentDarkColor);
    // 边缘左/右滑 = 返回上一级（手机全面屏手势）
    view::set_edge_back_callback([this]() { goBack(); });
    // 离开 LvglLockGuard 后再扫盘（SD I/O 期间不能让 LVGL 任务拿到时间片）
}

void AppMibaoFiles::onRunning() {
    if (_close_requested) {
        _close_requested = false;
        close();
        return;
    }
    // 批量转纪要完成：重扫列表显示新生成的 .txt
    if (_convert_done_refresh.exchange(false)) {
        scanFiles();
        refreshList();
        refreshStatus();
        syncButtons();
    }
    // 播放进行中允许退出：后台 task 自带 stop 标志
    {
        LvglLockGuard lock;
        view::update_home_indicator();
        view::update_status_bar();
    }
}

void AppMibaoFiles::onClose() {
    mclog::tagInfo(getAppInfo().name, "on close");
    _stop_play = true;  // 通知后台播放 task 退出

    LvglLockGuard lock;
    view::clear_edge_back_callback();
    destroyDialog();
    destroyUi();
    view::destroy_home_indicator();
    view::destroy_status_bar();
}

void AppMibaoFiles::createUi() {
    _root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_root, 320, 240);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // 标题：首页"文件管理"，进入目录后显示目录名
    _title_label = create_label(_root, "文件管理", &mibao_zh_font_16, 0xFFFFFF);
    lv_obj_set_width(_title_label, 180);
    lv_obj_align(_title_label, LV_ALIGN_TOP_LEFT, 12, 6);

    // 计数标签：右上角（与标题同排右上）
    _title_count_label = create_label(_root, "共 0 个文件", &mibao_zh_font_16, 0xFFFFFF);
    lv_obj_set_width(_title_count_label, 150);
    lv_obj_set_style_text_align(_title_count_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_title_count_label, LV_ALIGN_TOP_RIGHT, -12, 6);

    // 文件列表（加高，占满到底部按钮上方；从标题下方开始）
    _list = lv_obj_create(_root);
    lv_obj_set_size(_list, 296, 150);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(_list, lv_color_hex(kListBg), 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list, 1, 0);
    lv_obj_set_style_border_color(_list, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_radius(_list, 6, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    // 按钮行：屏幕左下角 4 个按钮均分（播放/重命名/删除/转纪要），
    // 总宽 238（5×46 + 4×2），与右下角 home_indicator 仅轻微重叠（~10px，可接受）
    lv_obj_t* btn_row = lv_obj_create(_root);
    lv_obj_set_size(btn_row, 238, 36);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 2, 0);  // 按钮间水平间距 2px
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    // 返回上一级（进入目录后显示；首页隐藏）
    _back_btn = create_button(btn_row, "返回", kAccentDarkColor, &mibao_zh_font_16);
    lv_obj_set_size(_back_btn, 46, 36);
    lv_obj_add_flag(_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _back_btn,
        [](lv_event_t* e) {
            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
            if (view::swipe_recently_detected(400)) {
                return;
            }
            app->goBack();
        },
        LV_EVENT_CLICKED, this);

    _btn_play = create_button(btn_row, "播放", kAccentColor, &mibao_zh_font_16);
    lv_obj_set_size(_btn_play, 46, 36);
    _btn_play_label = lv_obj_get_child(_btn_play, 0);
    lv_obj_add_event_cb(_btn_play,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            if (view::swipe_recently_detected(400)) {
                                return;  // 滑动呼出 home/状态栏时抑制误触
                            }
                            app->playSelected();
                        },
                        LV_EVENT_CLICKED, this);

    _btn_rename = create_button(btn_row, "重命名", kAccentDarkColor, &mibao_zh_font_16);
    lv_obj_set_size(_btn_rename, 46, 36);
    _btn_rename_label = lv_obj_get_child(_btn_rename, 0);
    lv_obj_add_event_cb(_btn_rename,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            if (view::swipe_recently_detected(400)) {
                                return;  // 滑动呼出 home/状态栏时抑制误触
                            }
                            app->openRenameDialog();
                        },
                        LV_EVENT_CLICKED, this);

    _btn_delete = create_button(btn_row, "删除", kDangerColor, &mibao_zh_font_16);
    lv_obj_set_size(_btn_delete, 46, 36);
    _btn_delete_label = lv_obj_get_child(_btn_delete, 0);
    lv_obj_add_event_cb(_btn_delete,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            if (view::swipe_recently_detected(400)) {
                                return;  // 滑动呼出 home/状态栏时抑制误触
                            }
                            app->deleteSelected();
                        },
                        LV_EVENT_CLICKED, this);

    // 转纪要：把选中的录音/视频转成纪要
    _btn_convert = create_button(btn_row, "转纪要", kAccentColor, &mibao_zh_font_16);
    lv_obj_set_size(_btn_convert, 46, 36);
    lv_obj_add_event_cb(_btn_convert,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            if (view::swipe_recently_detected(400)) {
                                return;  // 滑动呼出 home/状态栏时抑制误触
                            }
                            app->startConvertAll();
                        },
                        LV_EVENT_CLICKED, this);

    syncButtons();

    // ---- 删除确认弹窗（全屏遮罩 + 对话框，默认隐藏） ----
    _confirm_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(_confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(_confirm_panel, 320, 240);
    lv_obj_align(_confirm_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_confirm_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_confirm_panel, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_confirm_panel, 0, 0);
    lv_obj_set_style_radius(_confirm_panel, 0, 0);
    lv_obj_add_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* confirm_dialog = lv_obj_create(_confirm_panel);
    lv_obj_set_size(confirm_dialog, 280, 140);
    lv_obj_center(confirm_dialog);
    lv_obj_set_style_bg_color(confirm_dialog, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_border_width(confirm_dialog, 2, 0);
    lv_obj_set_style_border_color(confirm_dialog, lv_color_hex(kDangerColor), 0);
    lv_obj_set_style_radius(confirm_dialog, 8, 0);
    lv_obj_set_style_pad_all(confirm_dialog, 6, 0);
    lv_obj_clear_flag(confirm_dialog, LV_OBJ_FLAG_SCROLLABLE);

    _confirm_label = create_label(confirm_dialog, "确定删除？", &mibao_zh_font_16, kInkColor);
    lv_obj_set_width(_confirm_label, 230);
    lv_label_set_long_mode(_confirm_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(_confirm_label, LV_ALIGN_TOP_MID, 0, 16);

    _confirm_cancel = create_button(confirm_dialog, "取消", kAccentDarkColor, &mibao_zh_font_16);
    lv_obj_set_size(_confirm_cancel, 96, 36);
    lv_obj_align(_confirm_cancel, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_add_event_cb(_confirm_cancel,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            app->hideDeleteConfirm();
                        },
                        LV_EVENT_CLICKED, this);

    _confirm_ok = create_button(confirm_dialog, "删除", kDangerColor, &mibao_zh_font_16);
    lv_obj_set_size(_confirm_ok, 96, 36);
    lv_obj_align(_confirm_ok, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_add_event_cb(_confirm_ok,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            app->hideDeleteConfirm();
                            app->doDeleteConfirmed();
                        },
                        LV_EVENT_CLICKED, this);

    // 离 LVGL 锁后扫盘（SD I/O 不能持锁）
    {
        hal_bridge::board_sdcard_try_remount();
        scanFiles();
    }
    LvglLockGuard lock;
    refreshList();
    refreshStatus();
}

void AppMibaoFiles::destroyUi() {
    if (_confirm_panel != nullptr) {
        lv_obj_delete(_confirm_panel);
    }
    _confirm_panel = nullptr;
    _confirm_label = nullptr;
    _confirm_ok = nullptr;
    _confirm_cancel = nullptr;
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
    _root = nullptr;
    _list = nullptr;
    _title_label = nullptr;
    _title_count_label = nullptr;
    _back_btn = nullptr;
    _btn_play = nullptr;
    _btn_delete = nullptr;
    _btn_rename = nullptr;
    _btn_convert = nullptr;
    _btn_play_label = nullptr;
    _btn_delete_label = nullptr;
    _btn_rename_label = nullptr;
}

void AppMibaoFiles::destroyDialog() {
    if (_dialog != nullptr) {
        lv_obj_delete(_dialog);
    }
    _dialog = nullptr;
    _dialog_textarea = nullptr;
    _dialog_keyboard = nullptr;
}

void AppMibaoFiles::startConvertAll() {
    if (_convert_running.load()) {
        return;
    }
    // 单文件转换：转当前选中的录音/视频
    if (_selected_index < 0 || _selected_index >= static_cast<int>(_files.size())) {
        view::pop_a_toast("请先选择要转换的文件", view::ToastType::Warning);
        return;
    }
    const std::string path = _files[_selected_index];
    // .txt 本身已是纪要，无需再转
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".txt") {
        view::pop_a_toast("这是纪要文件，无需转换", view::ToastType::Info);
        return;
    }

    const std::string url = mibao::getUploadUrl();
    if (url.empty()) {
        view::pop_a_toast("未配置上传地址", view::ToastType::Warning);
        return;
    }
    // 构造待转换项（后台任务只处理这一个文件）
    _convert_pending_path = path;
    const auto slash = path.rfind('/');
    _convert_pending_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    _convert_pending_type = (path.find("/personal/") != std::string::npos) ? "personal"
                                                                           : "meeting";

    _convert_running.store(true);
    view::pop_a_toast("开始转换", view::ToastType::Info);
    if (xTaskCreate(
            [](void* arg) {
                auto* app = static_cast<AppMibaoFiles*>(arg);
                app->convertAllTask();
                app->_convert_running.store(false);
                vTaskDelete(nullptr);
            },
            "files_convert", 12288, this, 2, nullptr) != pdPASS) {
        _convert_running.store(false);
        view::pop_a_toast("转换任务启动失败", view::ToastType::Error);
    }
}

void AppMibaoFiles::convertAllTask() {
    // 单文件转换：只处理 startConvertAll 选中的那一个
    const std::string path = _convert_pending_path;
    const std::string name = _convert_pending_name;
    const std::string type = _convert_pending_type;
    if (path.empty()) {
        view::pop_a_toast("没有待转换的文件", view::ToastType::Info);
        return;
    }

    const std::string url = mibao::getUploadUrl();
    const bool ok = hal_bridge::upload_recording_for_notes(url, path, name, type);
    if (ok) {
        view::pop_a_toast("转换成功", view::ToastType::Info);
    } else {
        view::pop_a_toast("转换失败", view::ToastType::Error);
    }
    _convert_done_refresh.store(true);
}

void AppMibaoFiles::scanFiles() {
    _files.clear();
    _labels.clear();
    _row_is_header.clear();
    _folders.clear();
    _wav_count = 0;
    _avi_count = 0;

    if (!hal_bridge::board_sdcard_is_mounted()) {
        // 尝试重挂一次
        if (!hal_bridge::board_sdcard_try_remount()) {
            return;
        }
    }

    SdCardAccessGuard guard;

    if (_browsing_dir.empty()) {
        // ---- 首页：列出 /sdcard 下的子目录（文件夹） ----
        DIR* d = opendir("/sdcard");
        if (d != nullptr) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                const std::string name = ent->d_name;
                if (name[0] == '.') continue;
                if (ent->d_type != DT_DIR) continue;
                // 过滤系统/隐藏目录（Windows 在 SD 卡上自动创建的）
                if (name == "System Volume Information" || name == "$RECYCLE.BIN" ||
                    name == "RECYCLER" || name == ".Trashes" || name == ".fseventsd" ||
                    name == ".Spotlight-V100" || name == ".TemporaryItems") {
                    continue;
                }
                const std::string full = std::string("/sdcard/") + name;
                // 显示名：目录名（meetings → 会议，personal → 个人灵感）
                std::string display = name;
                if (name == "meetings") display = "会议";
                else if (name == "personal") display = "个人灵感";
                _folders.push_back(full);
                _labels.push_back("[目录] " + display);
            }
            closedir(d);
        }
        // 目录排序：meetings/personal 优先，其余按名
        std::sort(_folders.begin(), _folders.end());
        return;
    }

    // ---- 目录内：按类型分组收集（录音 / 视频 / 纪要），避免混排 ----
    std::vector<std::string> rec_files, rec_labels;   // 录音 .wav
    std::vector<std::string> vid_files, vid_labels;   // 视频 .avi
    std::vector<std::string> txt_files, txt_labels;   // 纪要 .txt
    DIR* d = opendir(_browsing_dir.c_str());
    if (d != nullptr) {
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            const std::string name = ent->d_name;
            if (name[0] == '.') continue;
            const bool is_wav = name.size() >= 5 && name.substr(name.size() - 4) == ".wav";
            const bool is_avi = name.size() >= 5 && name.substr(name.size() - 4) == ".avi";
            const bool is_txt = name.size() >= 5 && name.substr(name.size() - 4) == ".txt";
            if (!is_wav && !is_avi && !is_txt) continue;
            const std::string full_path = _browsing_dir + "/" + name;
            if (is_avi) {
                vid_files.push_back(full_path);
                vid_labels.push_back(name);
                _avi_count++;
            } else if (is_txt) {
                txt_files.push_back(full_path);
                txt_labels.push_back(name);
                _wav_count++;  // 纪要随录音统计，不影响现有计数语义
            } else {
                rec_files.push_back(full_path);
                rec_labels.push_back(name);
                _wav_count++;
            }
        }
        closedir(d);
    }
    // 每组倒序：最新在前
    std::reverse(rec_files.begin(), rec_files.end());
    std::reverse(rec_labels.begin(), rec_labels.end());
    std::reverse(vid_files.begin(), vid_files.end());
    std::reverse(vid_labels.begin(), vid_labels.end());
    std::reverse(txt_files.begin(), txt_files.end());
    std::reverse(txt_labels.begin(), txt_labels.end());

    // 合并：标题行（_files 空串 + header 标记）+ 文件行
    auto append_group = [this](const std::string& header, const std::vector<std::string>& files,
                               const std::vector<std::string>& labels) {
        if (files.empty()) {
            return;
        }
        _files.push_back("");          // 标题行无路径
        _labels.push_back(header);
        _row_is_header.push_back(true);
        for (size_t i = 0; i < files.size(); ++i) {
            _files.push_back(files[i]);
            _labels.push_back(labels[i]);
            _row_is_header.push_back(false);
        }
    };
    append_group("── 录音 ──", rec_files, rec_labels);
    append_group("── 视频 ──", vid_files, vid_labels);
    append_group("── 纪要 ──", txt_files, txt_labels);
}

void AppMibaoFiles::enterFolder(const std::string& dir) {
    _browsing_dir = dir;
    _selected_index = -1;
    scanFiles();
    refreshList();
    refreshStatus();
    syncButtons();
    // 更新标题与返回按钮
    const size_t slash = dir.rfind('/');
    std::string name = (slash == std::string::npos) ? dir : dir.substr(slash + 1);
    if (name == "meetings") name = "会议";
    else if (name == "personal") name = "个人灵感";
    set_label(_title_label, name.c_str());
    lv_obj_clear_flag(_back_btn, LV_OBJ_FLAG_HIDDEN);
}

void AppMibaoFiles::goBack() {
    if (_browsing_dir.empty()) {
        return;  // 已在首页
    }
    _browsing_dir.clear();
    _selected_index = -1;
    scanFiles();
    refreshList();
    refreshStatus();
    syncButtons();
    set_label(_title_label, "文件管理");
    lv_obj_add_flag(_back_btn, LV_OBJ_FLAG_HIDDEN);
}

void AppMibaoFiles::refreshList() {
    // 清空现有行
    while (lv_obj_get_child_cnt(_list) > 0) {
        lv_obj_delete(lv_obj_get_child(_list, 0));
    }

    // ---- 首页：显示文件夹列表 ----
    if (_browsing_dir.empty()) {
        if (_folders.empty()) {
            lv_obj_t* empty = create_label(_list, "暂无文件夹", &mibao_zh_font_16, 0x808080);
            lv_obj_set_width(empty, 280);
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            return;
        }
        for (size_t i = 0; i < _folders.size(); ++i) {
            lv_obj_t* row = lv_obj_create(_list);
            lv_obj_set_size(row, LV_PCT(100), 36);
            lv_obj_set_style_bg_color(row, lv_color_hex(kListBg), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(0xEEEEEE), 0);
            lv_obj_set_style_pad_left(row, 6, 0);
            lv_obj_set_style_pad_right(row, 6, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            const bool folder_selected = (static_cast<int>(i) == _selected_index);
            lv_obj_t* lbl = create_label(row, _labels[i].c_str(), &mibao_zh_font_16,
                                         folder_selected ? kListSelText : kAccentDarkColor);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_width(lbl, 280);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            if (folder_selected) {
                lv_obj_set_style_bg_color(row, lv_color_hex(kListSelBg), 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            }
            // 文件夹：第一次点击 = 选中（可重命名/删除）；再点已选中的 = 进入
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(i + 1)));
            lv_obj_add_event_cb(row,
                                [](lv_event_t* e) {
                                    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                    auto raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj));
                                    int idx = static_cast<int>(raw) - 1;
                                    auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                                    if (idx < 0 || idx >= static_cast<int>(app->_folders.size())) {
                                        return;
                                    }
                                    if (app->_selected_index == idx) {
                                        app->enterFolder(app->_folders[idx]);  // 再点进入
                                    } else {
                                        app->setSelected(idx);  // 第一次点选中
                                    }
                                },
                                LV_EVENT_CLICKED, this);
        }
        return;
    }

    // ---- 目录内：文件列表 ----
    if (_files.empty()) {
        lv_obj_t* empty = create_label(_list, "暂无文件", &mibao_zh_font_16, 0x808080);
        lv_obj_set_width(empty, 280);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    for (size_t i = 0; i < _labels.size(); ++i) {
        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 28);
        lv_obj_set_style_bg_color(row, lv_color_hex(kListBg), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // 分组标题行：灰色文字、不可选中
        const bool is_header = (i < _row_is_header.size()) && _row_is_header[i];
        if (is_header) {
            lv_obj_t* hdr = create_label(row, _labels[i].c_str(), &mibao_zh_font_16, 0x9AA0A6);
            lv_obj_align(hdr, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_width(hdr, 280);
            lv_label_set_long_mode(hdr, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
            continue;
        }

        const bool selected = (static_cast<int>(i) == _selected_index);
        lv_obj_t* lbl = create_label(row, _labels[i].c_str(), &mibao_zh_font_16,
                                     selected ? kListSelText : kInkColor);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        // 关键修复：必须先 setWidth 再 set_long_mode，且 long_mode 设完
        // 触发一次 invalidate 才能让 LVGL 重新计算截断。
        // 之前 create_label 内 long_mode 设得太早（width=0），
        // re-layout 时 width 270 已经设进去但 long_mode 标志可能被吞。
        lv_obj_set_width(lbl, 280);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        // 单行强制：DOT 模式需要单行才生效
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        if (selected) {
            lv_obj_set_style_bg_color(row, lv_color_hex(kListSelBg), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        }

        auto idx = static_cast<int>(i);
        // LVGL 9 支持 lv_obj_set_user_data(obj, ptr)，与事件 user_data 互不冲突。
        // 用对象级 user_data 携带行索引，回调里读取。
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(idx + 1)));
        lv_obj_add_event_cb(row,
                            [](lv_event_t* e) {
                                auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                auto raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj));
                                int idx = static_cast<int>(raw) - 1;
                                auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                                app->setSelected(idx);
                            },
                            LV_EVENT_CLICKED, this);
    }
}

void AppMibaoFiles::refreshStatus() {
    char buf[64];
    if (_browsing_dir.empty()) {
        std::snprintf(buf, sizeof(buf), "共 %u 个文件夹", static_cast<unsigned>(_folders.size()));
    } else {
        // 减去分组标题行，只统计真实文件数
        unsigned file_count = 0;
        for (size_t i = 0; i < _row_is_header.size(); ++i) {
            if (!_row_is_header[i]) file_count++;
        }
        std::snprintf(buf, sizeof(buf), "共 %u 个文件", file_count);
    }
    set_label(_title_count_label, buf);
}

void AppMibaoFiles::setSelected(int index) {
    const size_t count = _browsing_dir.empty() ? _folders.size() : _files.size();
    if (index < 0 || index >= static_cast<int>(count)) {
        _selected_index = -1;
    } else if (!_browsing_dir.empty() && index < static_cast<int>(_row_is_header.size()) &&
               _row_is_header[index]) {
        _selected_index = -1;  // 分组标题行不可选中
    } else {
        _selected_index = index;
    }
    refreshList();
    syncButtons();
}

void AppMibaoFiles::syncButtons() {
    // 返回按钮：进入目录后显示，首页隐藏
    if (_back_btn != nullptr) {
        if (!_browsing_dir.empty()) {
            lv_obj_clear_flag(_back_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 首页：选中文件夹 → 可重命名/删除；播放/转纪要禁用
    if (_browsing_dir.empty()) {
        const bool folder_sel = (_selected_index >= 0 && !_playing);
        if (_btn_play != nullptr) {
            lv_obj_add_state(_btn_play, LV_STATE_DISABLED);
        }
        if (_btn_play_label != nullptr) {
            set_label(_btn_play_label, "播放");
        }
        if (_btn_rename != nullptr) {
            if (folder_sel) lv_obj_clear_state(_btn_rename, LV_STATE_DISABLED);
            else lv_obj_add_state(_btn_rename, LV_STATE_DISABLED);
        }
        if (_btn_delete != nullptr) {
            if (folder_sel) lv_obj_clear_state(_btn_delete, LV_STATE_DISABLED);
            else lv_obj_add_state(_btn_delete, LV_STATE_DISABLED);
        }
        if (_btn_convert != nullptr) {
            lv_obj_add_state(_btn_convert, LV_STATE_DISABLED);
        }
        return;
    }

    // 目录内：选中文件类型（.txt 纪要 / .wav 录音 / .avi 视频）
    bool is_txt = false;
    bool is_media = false;  // 录音或视频
    if (_selected_index >= 0 && _selected_index < static_cast<int>(_files.size())) {
        const std::string& p = _files[_selected_index];
        if (p.size() >= 4 && p.substr(p.size() - 4) == ".txt") is_txt = true;
        else if (p.size() >= 4 && (p.substr(p.size() - 4) == ".wav" || p.substr(p.size() - 4) == ".avi")) is_media = true;
    }
    const bool enabled = (_selected_index >= 0 && !_playing);

    // 播放/查看：.txt 显示"查看"；录音/视频显示"播放"
    if (_btn_play != nullptr) {
        if (enabled && (is_media || is_txt)) {
            lv_obj_clear_state(_btn_play, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_btn_play, LV_STATE_DISABLED);
        }
    }
    if (_btn_play_label != nullptr) {
        set_label(_btn_play_label, _playing ? "播放中" : (is_txt ? "查看" : "播放"));
    }
    // 重命名/删除：任何文件都可（含 .txt）
    if (_btn_rename != nullptr) {
        if (enabled) {
            lv_obj_clear_state(_btn_rename, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_btn_rename, LV_STATE_DISABLED);
        }
    }
    if (_btn_delete != nullptr) {
        if (enabled) {
            lv_obj_clear_state(_btn_delete, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_btn_delete, LV_STATE_DISABLED);
        }
    }
    // 转纪要：仅录音(.wav)/视频(.avi) 可转；.txt 已是纪要不可转
    if (_btn_convert != nullptr) {
        if (is_media && !_playing) {
            lv_obj_clear_state(_btn_convert, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_btn_convert, LV_STATE_DISABLED);
        }
    }
}

void AppMibaoFiles::playSelected() {
    if (_selected_index < 0 || _selected_index >= static_cast<int>(_files.size()) || _playing) {
        return;
    }
    const std::string path = _files[_selected_index];

    // 纪要(.txt)：读取内容并在对话框里展示
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".txt") {
        if (!hal_bridge::board_sdcard_is_mounted() && !hal_bridge::board_sdcard_try_remount()) {
            view::pop_a_toast("SD 卡未挂载", view::ToastType::Error);
            return;
        }
        std::string content;
        {
            SdCardAccessGuard guard;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f == nullptr) {
                view::pop_a_toast("无法打开纪要", view::ToastType::Error);
                return;
            }
            std::fseek(f, 0, SEEK_END);
            const long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < 8192) {
                content.resize(static_cast<size_t>(sz));
                const size_t got = std::fread(&content[0], 1, static_cast<size_t>(sz), f);
                content.resize(got);
            }
            std::fclose(f);
        }
        if (content.empty()) {
            view::pop_a_toast("纪要为空", view::ToastType::Warning);
            return;
        }
        // 展示对话框：正文独立滚动，按钮固定底部
        lv_obj_t* dlg = lv_obj_create(lv_screen_active());
        lv_obj_set_size(dlg, 300, 220);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(kPanelSoftColor), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(kAccentColor), 0);
        lv_obj_set_style_radius(dlg, 8, 0);
        lv_obj_set_style_pad_all(dlg, 6, 0);
        lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = create_label(dlg, "会议纪要", &mibao_zh_font_16, kAccentDarkColor);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

        // 正文：独立可滚动容器（标题下方、按钮上方），滚动时按钮不动
        lv_obj_t* body_area = lv_obj_create(dlg);
        lv_obj_set_size(body_area, 280, 150);
        lv_obj_align(body_area, LV_ALIGN_TOP_MID, 0, 24);
        lv_obj_set_style_bg_opa(body_area, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body_area, 0, 0);
        lv_obj_set_style_pad_all(body_area, 2, 0);
        lv_obj_set_style_radius(body_area, 0, 0);
        lv_obj_set_scroll_dir(body_area, LV_DIR_VER);
        lv_obj_add_flag(body_area, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* body = lv_label_create(body_area);
        lv_label_set_text(body, content.c_str());
        lv_obj_set_style_text_font(body, &mibao_zh_font_16, 0);
        // 深色文字，浅色背景上清晰可读
        lv_obj_set_style_text_color(body, lv_color_hex(0x273238), 0);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, 270);

        lv_obj_t* btn_close = create_button(dlg, "关闭", kAccentColor, &mibao_zh_font_16);
        lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_add_event_cb(btn_close,
                            [](lv_event_t* e) {
                                lv_obj_t* obj = lv_event_get_target_obj(e);
                                lv_obj_delete(lv_obj_get_parent(obj));
                            },
                            LV_EVENT_CLICKED, nullptr);
        return;
    }

    // 视频(.avi)：设置待播放路径，跳转到视频播放器（AppMibaoVideo，appID=3）播放
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".avi") {
        hal_bridge::set_pending_video_playback(path);
        GetMooncake().openApp(3);
        _close_requested = true;
        return;
    }

    // 在主线程先做 fopen + 解析 WAV 头验证文件合法，避免 task 启动后才发现错误。
    // （主线程短时持锁，LVGL 任务偶尔等几十 ms，比 task 启动后再炸可控。）
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint64_t data_offset = 0;
    uint64_t data_bytes = 0;
    {
        if (!hal_bridge::board_sdcard_is_mounted() &&
            !hal_bridge::board_sdcard_try_remount()) {
            view::pop_a_toast("SD 卡未挂载", view::ToastType::Error);
            return;
        }
        SdCardAccessGuard guard;
        FILE* probe = std::fopen(path.c_str(), "rb");
        if (probe == nullptr) {
            view::pop_a_toast("无法打开文件", view::ToastType::Error);
            return;
        }
        const bool ok = parse_wav_header(probe, sample_rate, channels, bits, data_offset, data_bytes);
        std::fclose(probe);
        if (!ok) {
            view::pop_a_toast("WAV 格式无效", view::ToastType::Error);
            return;
        }
        if (channels != 1 || sample_rate != kSampleRateRecord) {
            mclog::tagWarn("AppMibaoFiles", "unsupported wav: %uHz %uch", sample_rate, channels);
            view::pop_a_toast("不支持的采样率", view::ToastType::Error);
            return;
        }
    }

    _stop_play = false;
    _playing = true;
    syncButtons();

    // task 传：路径 + 解析好的元信息。避免 task 内部再次做 fopen/parse 浪费栈。
    struct PlayTaskArg {
        std::string path;
        uint64_t data_offset;
        uint64_t data_bytes;
        AppMibaoFiles* self;
    };
    auto* task_arg = new PlayTaskArg{path, data_offset, data_bytes, this};
    // 栈 32KB：原 16KB 不够，stackchan 框架 task 局部变量多，且 codec 调用链深。
    if (xTaskCreate(
            [](void* arg) {
                auto* task_arg = static_cast<PlayTaskArg*>(arg);
                const std::string p = std::move(task_arg->path);
                const uint64_t data_offset = task_arg->data_offset;
                const uint64_t data_bytes = task_arg->data_bytes;
                AppMibaoFiles* self = task_arg->self;
                delete task_arg;

                // 关键：每次 fread 单独持 SdCardAccessGuard，绝不持锁跨 codec 操作
                // 1) 持锁时不让出 SD 总线给其他任务
                // 2) codec->OutputData 是 I2S DMA，可能阻塞数十 ms，期间绝不能持 SD 锁
                FILE* f = nullptr;
                {
                    SdCardAccessGuard guard;
                    f = std::fopen(p.c_str(), "rb");
                    if (f == nullptr) {
                        self->_playing = false;
                        return;
                    }
                    std::fseek(f, static_cast<long>(data_offset), SEEK_SET);
                }

                auto& board = Board::GetInstance();
                auto codec = board.GetAudioCodec();
                if (codec == nullptr) {
                    std::fclose(f);
                    self->_playing = false;
                    return;
                }
                codec->EnableOutput(true);

                // 16k→24k 升采样：每 2 输入 → 3 输出。
                constexpr int kInFramesPerChunk = 256;  // 减小每块，降低单次持锁时间
                std::vector<int16_t> in_buf(kInFramesPerChunk);
                std::vector<int16_t> out_buf((kInFramesPerChunk * 3) / 2 + 8);

                const uint64_t total_input_frames = data_bytes / 2;  // 16bit 单声道
                uint64_t frames_played = 0;

                while (frames_played < total_input_frames && !self->_stop_play) {
                    // 1) 短持锁读 SD
                    size_t got = 0;
                    {
                        SdCardAccessGuard guard;
                        const size_t want = std::min<uint64_t>(kInFramesPerChunk,
                                                              total_input_frames - frames_played);
                        got = std::fread(in_buf.data(), sizeof(int16_t), want, f);
                    }
                    if (got == 0) break;
                    // 2) 升采样（不持锁）
                    size_t out_frames = 0;
                    size_t i = 0;
                    while (i + 1 < got) {
                        upsample_2to3(&in_buf[i], &out_buf[out_frames]);
                        out_frames += 3;
                        i += 2;
                    }
                    if (i < got) {
                        const int16_t pair[2] = {in_buf[i], in_buf[i]};
                        upsample_2to3(pair, &out_buf[out_frames]);
                        out_frames += 3;
                    }
                    // 3) 输出到 codec（不持锁，codec 内部 DMA 异步）
                    std::vector<int16_t> chunk(out_buf.begin(), out_buf.begin() + out_frames);
                    codec->OutputData(chunk);
                    frames_played += got;
                    // 4) 让出 CPU，避免饿死 LVGL 任务（实测 24k 立体声下 DMA 缓冲足够）
                    vTaskDelay(pdMS_TO_TICKS(1));
                }

                // 关闭文件
                {
                    SdCardAccessGuard guard;
                    std::fclose(f);
                }
                // 给 codec 时间推完最后一段
                vTaskDelay(pdMS_TO_TICKS(200));
                codec->EnableOutput(false);
                self->_playing = false;
            },
            "files_play", 32768, task_arg, 4, nullptr) != pdPASS) {
        delete task_arg;
        _playing = false;
        syncButtons();
        view::pop_a_toast("播放启动失败", view::ToastType::Error);
    }
}

// 递归删除目录及其全部内容（文件 + 子目录）
static bool removeDirectoryRecursive(const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (d == nullptr) {
        return false;
    }
    struct dirent* ent;
    bool ok = true;
    while ((ent = readdir(d)) != nullptr) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        const std::string full = path + "/" + name;
        if (ent->d_type == DT_DIR) {
            if (!removeDirectoryRecursive(full)) ok = false;
        } else {
            if (::unlink(full.c_str()) != 0) ok = false;
        }
    }
    closedir(d);
    if (::rmdir(path.c_str()) != 0) ok = false;
    return ok;
}

void AppMibaoFiles::deleteSelected() {
    // 有选中项才弹确认框（不直接删）
    if (_browsing_dir.empty()) {
        if (_selected_index < 0 || _selected_index >= static_cast<int>(_folders.size())) {
            return;
        }
    } else {
        if (_selected_index < 0 || _selected_index >= static_cast<int>(_files.size())) {
            return;
        }
    }
    showDeleteConfirm();
}

void AppMibaoFiles::showDeleteConfirm() {
    if (_confirm_panel == nullptr) {
        return;
    }
    // 提示语：文件夹/文件
    std::string msg = "确定删除？";
    if (!_browsing_dir.empty() && _selected_index >= 0 &&
        _selected_index < static_cast<int>(_labels.size())) {
        const std::string& nm = _labels[_selected_index];
        msg = "确定删除 " + nm + "？";
    }
    if (_confirm_label != nullptr) {
        set_label(_confirm_label, msg.c_str());
    }
    lv_obj_clear_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_confirm_panel);
}

void AppMibaoFiles::hideDeleteConfirm() {
    if (_confirm_panel != nullptr) {
        lv_obj_add_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppMibaoFiles::doDeleteConfirmed() {
    // 首页：删除选中的文件夹；目录内：删除选中的文件
    if (_browsing_dir.empty()) {
        if (_selected_index < 0 || _selected_index >= static_cast<int>(_folders.size())) {
            return;
        }
        const std::string path = _folders[_selected_index];
        {
            SdCardAccessGuard guard;
            if (!removeDirectoryRecursive(path)) {
                mclog::tagError("AppMibaoFiles", "rmdir failed: %s", path.c_str());
                view::pop_a_toast("删除失败", view::ToastType::Error);
                return;
            }
        }
        _folders.erase(_folders.begin() + _selected_index);
        _labels.erase(_labels.begin() + _selected_index);
        _selected_index = -1;
        refreshList();
        refreshStatus();
        syncButtons();
        view::pop_a_toast("已删除", view::ToastType::Info);
        return;
    }

    if (_selected_index < 0 || _selected_index >= static_cast<int>(_files.size())) {
        return;
    }
    const std::string path = _files[_selected_index];
    {
        SdCardAccessGuard guard;
        if (::unlink(path.c_str()) != 0) {
            const int err = errno;
            mclog::tagError("AppMibaoFiles", "unlink failed: %s, errno=%d", path.c_str(), err);
            view::pop_a_toast("删除失败", view::ToastType::Error);
            return;
        }
    }
    _files.erase(_files.begin() + _selected_index);
    _labels.erase(_labels.begin() + _selected_index);
    _selected_index = -1;
    refreshList();
    refreshStatus();
    syncButtons();
    view::pop_a_toast("已删除", view::ToastType::Info);
}

void AppMibaoFiles::openRenameDialog() {
    // 首页：重命名文件夹；目录内：重命名文件
    _renaming_folder = _browsing_dir.empty();
    std::string path;
    if (_renaming_folder) {
        if (_selected_index < 0 || _selected_index >= static_cast<int>(_folders.size())) {
            return;
        }
        path = _folders[_selected_index];
    } else {
        if (_selected_index < 0 || _selected_index >= static_cast<int>(_files.size())) {
            return;
        }
        path = _files[_selected_index];
    }
    const std::string dir = path.substr(0, path.rfind('/'));
    const std::string name = path.substr(path.rfind('/') + 1);
    // 保留原后缀（.wav/.avi/.txt）供用户编辑；文件夹无后缀
    const std::string base = name;

    _rename_old_path = path;
    _rename_old_name = name;

    _dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_dialog, 280, 180);
    lv_obj_center(_dialog);
    lv_obj_set_style_bg_color(_dialog, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_border_width(_dialog, 2, 0);
    lv_obj_set_style_border_color(_dialog, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(_dialog, 8, 0);
    lv_obj_set_style_pad_all(_dialog, 6, 0);
    lv_obj_clear_flag(_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = create_label(_dialog, "重命名", &mibao_zh_font_16, kAccentDarkColor);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    _dialog_textarea = lv_textarea_create(_dialog);
    lv_obj_set_size(_dialog_textarea, 260, 32);
    lv_obj_align(_dialog_textarea, LV_ALIGN_TOP_MID, 0, 22);
    lv_textarea_set_text(_dialog_textarea, base.c_str());
    lv_textarea_set_one_line(_dialog_textarea, true);
    lv_textarea_set_max_length(_dialog_textarea, 80);
    lv_obj_set_style_text_font(_dialog_textarea, &mibao_zh_font_16, 0);

    _dialog_keyboard = lv_keyboard_create(_dialog);
    lv_obj_set_size(_dialog_keyboard, 260, 100);
    lv_obj_align(_dialog_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(_dialog_keyboard, _dialog_textarea);

    // 确认按钮
    lv_obj_t* btn_ok = create_button(_dialog, "确定", kAccentColor, &mibao_zh_font_16);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    // 由于 keyboard 在底部，按钮放在右上
    lv_obj_align(btn_ok, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(btn_ok,
                        [](lv_event_t* e) {
                            auto* app = static_cast<AppMibaoFiles*>(lv_event_get_user_data(e));
                            const char* new_base = lv_textarea_get_text(app->_dialog_textarea);
                            if (new_base == nullptr || std::strlen(new_base) == 0) {
                                view::pop_a_toast("文件名不能为空", view::ToastType::Warning);
                                return;
                            }
                            const std::string dir = app->_rename_old_path.substr(0, app->_rename_old_path.rfind('/'));
                            // 文件夹无后缀；文件保留原后缀（.wav/.avi/.txt）
                            const std::string old_name = app->_rename_old_name;
                            const size_t dot = old_name.rfind('.');
                            const std::string ext =
                                (app->_renaming_folder || dot == std::string::npos)
                                    ? "" : old_name.substr(dot);
                            const std::string new_path = dir + "/" + new_base + ext;
                            if (new_path == app->_rename_old_path) {
                                view::pop_a_toast("未修改", view::ToastType::Info);
                                return;
                            }
                            {
                                SdCardAccessGuard guard;
                                if (::rename(app->_rename_old_path.c_str(), new_path.c_str()) != 0) {
                                    const int err = errno;
                                    mclog::tagError("AppMibaoFiles", "rename failed: %d", err);
                                    view::pop_a_toast("重命名失败", view::ToastType::Error);
                                    return;
                                }
                            }
                            // 同步更新列表
                            if (app->_renaming_folder) {
                                app->_folders[app->_selected_index] = new_path;
                                // 更新显示名（meetings → 会议 等）
                                std::string display = new_base;
                                if (display == "meetings") display = "会议";
                                else if (display == "personal") display = "个人灵感";
                                app->_labels[app->_selected_index] = "[目录] " + display;
                            } else {
                                app->_files[app->_selected_index] = new_path;
                                app->_labels[app->_selected_index] = std::string(new_base) + ext;
                            }
                            app->refreshList();
                            app->syncButtons();
                            view::pop_a_toast("已重命名", view::ToastType::Info);
                            // 关闭对话框
                            LvglLockGuard lock;
                            app->destroyDialog();
                        },
                        LV_EVENT_CLICKED, this);
}