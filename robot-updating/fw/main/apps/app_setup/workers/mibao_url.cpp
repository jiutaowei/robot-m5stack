/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// 米宝服务器地址配置：textarea + 虚拟键盘编辑 upload_url / iot_url，
// 读写 NVS 命名空间 "mibao"（hal/mibao_config）。
//
// 滑动防误触：手指按下后只要移动超过阈值（判定为滑动/滚动），
// 就不弹虚拟键盘、不保留输入框焦点 —— 滑动时不触发点击。
// 面板底部加高后可上下滚动，虚拟键盘挡住底部按钮时也能滚出来。
#include "workers.h"
#include <hal/mibao_config.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <lvgl.h>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-MibaoUrl";

namespace {

constexpr uint32_t kTextColor = 0x26206A;
constexpr uint32_t kHintColor = 0x8A8A9A;

// 手指移动超过该距离视为"滑动"，不触发点击/键盘
constexpr int32_t kSwipeThreshold = 16;
// 判定为滑动后，多长时间内的点击都算误触
constexpr uint32_t kSwipeSuppressMs = 400;
// 虚拟键盘高度（屏幕高度 50%，LVGL 默认）
constexpr lv_coord_t kKeyboardHeight = 120;
// 面板底部额外留白：键盘弹出后仍可滚动露出底部按钮
constexpr lv_coord_t kExtraScrollHeight = 128;

// 设置页同时只会存活一个 MibaoUrlWorker，用文件级静态状态即可
struct UrlContext {
    lv_obj_t* keyboard     = nullptr;
    lv_obj_t* panel        = nullptr;
    lv_obj_t* focused_ta   = nullptr;  // 本次按下获得焦点的输入框
    uint32_t focused_at_ms = 0;
    lv_obj_t* active_ta    = nullptr;  // 键盘当前绑定的输入框
    bool pressed           = false;    // 本次按下是否已记录起点
    bool moved             = false;    // 本次按下是否已判定为滑动
    uint32_t last_swipe_ms = 0;        // 最近一次判定为滑动的时刻
    lv_point_t press_start{0, 0};
};
UrlContext g_ctx;

lv_point_t current_point()
{
    lv_point_t p{0, 0};
    if (lv_indev_t* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_get_point(indev, &p);
    }
    return p;
}

void defocus_ta(lv_obj_t* ta)
{
    if (ta == nullptr) {
        return;
    }
    lv_obj_send_event(ta, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
}

void hide_keyboard()
{
    if (g_ctx.keyboard != nullptr && !lv_obj_has_flag(g_ctx.keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(g_ctx.keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    defocus_ta(g_ctx.active_ta);
    g_ctx.active_ta = nullptr;
}

void show_keyboard(lv_obj_t* ta)
{
    if (g_ctx.keyboard == nullptr || ta == nullptr) {
        return;
    }
    lv_keyboard_set_textarea(g_ctx.keyboard, ta);
    lv_obj_clear_flag(g_ctx.keyboard, LV_OBJ_FLAG_HIDDEN);
    g_ctx.active_ta = ta;
}

// 让输入框露在虚拟键盘上方（自动滚动面板）
void scroll_ta_above_keyboard(lv_obj_t* ta)
{
    if (g_ctx.panel == nullptr) {
        return;
    }
    const lv_coord_t ta_y   = lv_obj_get_y(ta);
    const lv_coord_t ta_h   = lv_obj_get_height(ta);
    const lv_coord_t kb_top = lv_obj_get_height(lv_screen_active()) - kKeyboardHeight;
    int32_t target          = ta_y + ta_h - kb_top + 10;
    if (target < 0) {
        target = 0;
    }
    lv_obj_scroll_to_y(g_ctx.panel, target, LV_ANIM_ON);
}

// ---- 输入框：按下记起点 + 焦点，释放确认非滑动后才弹键盘 ----

void on_ta_press(lv_event_t* e)
{
    g_ctx.pressed       = true;
    g_ctx.moved         = false;
    g_ctx.press_start   = current_point();
    g_ctx.focused_ta    = lv_event_get_target_obj(e);
    g_ctx.focused_at_ms = GetHAL().millis();
}

void on_ta_release(lv_event_t* e)
{
    const bool was_moved = g_ctx.moved;
    g_ctx.pressed = false;
    g_ctx.moved   = false;

    if (was_moved) {
        // 本次是滑动：不弹键盘（焦点已在滑动时清掉）
        return;
    }
    // 纯点击：刚按下的输入框弹键盘
    if (g_ctx.focused_ta != nullptr) {
        lv_obj_t* ta = g_ctx.focused_ta;
        g_ctx.focused_ta   = nullptr;
        g_ctx.focused_at_ms = 0;
        show_keyboard(ta);
        scroll_ta_above_keyboard(ta);
    }
    else {
        g_ctx.focused_ta   = nullptr;
        g_ctx.focused_at_ms = 0;
    }
}

// ---- 面板/按钮：只做滑动判定，不涉及输入框焦点 ----

void on_generic_press(lv_event_t* e)
{
    g_ctx.pressed       = true;
    g_ctx.moved         = false;
    g_ctx.press_start   = current_point();
    g_ctx.focused_ta    = nullptr;
    g_ctx.focused_at_ms = 0;
}

void on_generic_release(lv_event_t* e)
{
    const bool was_moved = g_ctx.moved;
    g_ctx.pressed = false;
    g_ctx.moved   = false;
    if (was_moved) {
        hide_keyboard();
    }
}

// ---- 滑动判定（按下后手指移动超过阈值）----

void on_press_move(lv_event_t* e)
{
    if (!g_ctx.pressed || g_ctx.moved) {
        return;
    }
    const lv_point_t p = current_point();
    if (LV_ABS(p.x - g_ctx.press_start.x) <= kSwipeThreshold &&
        LV_ABS(p.y - g_ctx.press_start.y) <= kSwipeThreshold) {
        return;
    }
    g_ctx.moved         = true;
    g_ctx.last_swipe_ms = GetHAL().millis();
    // 滑动不触发点击：收起键盘并清掉输入框焦点
    hide_keyboard();
    defocus_ta(g_ctx.focused_ta);
    g_ctx.focused_ta   = nullptr;
    g_ctx.focused_at_ms = 0;
}

// ---- 点面板空白处：收起键盘 ----

void on_panel_click(lv_event_t* e)
{
    if (lv_event_get_target(e) == g_ctx.panel) {
        hide_keyboard();
    }
}

// ---- 键盘：OK / 收起键 ----

void on_keyboard_ready(lv_event_t* e)
{
    lv_obj_add_flag(lv_event_get_target_obj(e), LV_OBJ_FLAG_HIDDEN);
    defocus_ta(g_ctx.active_ta);
    g_ctx.active_ta = nullptr;
}

void on_keyboard_cancel(lv_event_t* e)
{
    lv_obj_add_flag(lv_event_get_target_obj(e), LV_OBJ_FLAG_HIDDEN);
    defocus_ta(g_ctx.active_ta);
    g_ctx.active_ta = nullptr;
}

// 给任意对象挂滑动防误触：PRESSED / PRESSING / RELEASED
void attach_swipe_guard(lv_obj_t* obj, bool is_textarea)
{
    lv_obj_add_event_cb(obj, is_textarea ? on_ta_press : on_generic_press, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, on_press_move, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(obj, is_textarea ? on_ta_release : on_generic_release, LV_EVENT_RELEASED, nullptr);
}

void style_text_area(TextArea& ta)
{
    ta.setWidth(288);
    ta.setOneLine(true);
    ta.setHeight(26);
    ta.setBgColor(lv_color_hex(0xF4F4F8));
    ta.setBorderWidth(1);
    ta.setBorderColor(lv_color_hex(0xC8C8D4));
    ta.setRadius(8);
    ta.setTextFont(&lv_font_montserrat_16);
}

}  // namespace

MibaoUrlWorker::MibaoUrlWorker()
{
    mclog::tagInfo(_tag, "create");

    g_ctx = UrlContext{};

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(320, 240);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_hex(0xFFFFFF));
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setScrollDir(LV_DIR_VER);  // 内容可上下滚动：键盘挡住底部时能滚出来
    _panel->setPaddingAll(0);
    g_ctx.panel = _panel->get();
    attach_swipe_guard(_panel->get(), false);
    lv_obj_add_event_cb(_panel->get(), on_panel_click, LV_EVENT_CLICKED, nullptr);

    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setText("Mibao Server URLs");
    _label_title->setTextColor(lv_color_hex(kTextColor));
    _label_title->setTextFont(&lv_font_montserrat_20);
    _label_title->align(LV_ALIGN_TOP_MID, 0, 26);

    _label_ota_title = std::make_unique<Label>(_panel->get());
    _label_ota_title->setText("OTA URL (xiaozhi AI)");
    _label_ota_title->setTextColor(lv_color_hex(kHintColor));
    _label_ota_title->setTextFont(&lv_font_montserrat_16);
    _label_ota_title->align(LV_ALIGN_TOP_LEFT, 16, 46);

    _ta_ota = std::make_unique<TextArea>(_panel->get());
    style_text_area(*_ta_ota);
    _ta_ota->align(LV_ALIGN_TOP_MID, 0, 58);
    _ta_ota->setPlaceholderText("http://<ip>:8003/xiaozhi/ota/");
    _ta_ota->setText(mibao::getOtaUrl());
    attach_swipe_guard(_ta_ota->get(), true);

    _label_upload_title = std::make_unique<Label>(_panel->get());
    _label_upload_title->setText("Upload URL (empty = off)");
    _label_upload_title->setTextColor(lv_color_hex(kHintColor));
    _label_upload_title->setTextFont(&lv_font_montserrat_16);
    _label_upload_title->align(LV_ALIGN_TOP_LEFT, 16, 88);

    _ta_upload = std::make_unique<TextArea>(_panel->get());
    style_text_area(*_ta_upload);
    _ta_upload->align(LV_ALIGN_TOP_MID, 0, 100);
    _ta_upload->setPlaceholderText("http://...");
    _ta_upload->setText(mibao::getUploadUrl());
    attach_swipe_guard(_ta_upload->get(), true);

    _label_iot_title = std::make_unique<Label>(_panel->get());
    _label_iot_title->setText("IoT Actuator URL");
    _label_iot_title->setTextColor(lv_color_hex(kHintColor));
    _label_iot_title->setTextFont(&lv_font_montserrat_16);
    _label_iot_title->align(LV_ALIGN_TOP_LEFT, 16, 130);

    _ta_iot = std::make_unique<TextArea>(_panel->get());
    style_text_area(*_ta_iot);
    _ta_iot->align(LV_ALIGN_TOP_MID, 0, 142);
    _ta_iot->setPlaceholderText("http://10.51.1.205:5000");
    _ta_iot->setText(mibao::getIotUrl());
    attach_swipe_guard(_ta_iot->get(), true);

    // AI 对话开关：控制 app_chat 是否放开门控进入 xiaozhi（mibao/ai_chat）
    _label_ai_title = std::make_unique<Label>(_panel->get());
    _label_ai_title->setText("AI Chat");
    _label_ai_title->setTextColor(lv_color_hex(kTextColor));
    _label_ai_title->setTextFont(&lv_font_montserrat_16);
    _label_ai_title->align(LV_ALIGN_TOP_LEFT, 16, 174);

    _switch_ai = lv_switch_create(_panel->get());
    lv_obj_set_size(_switch_ai, 52, 26);
    lv_obj_align(_switch_ai, LV_ALIGN_TOP_RIGHT, -16, 172);
    if (mibao::isAiChatEnabled()) {
        lv_obj_add_state(_switch_ai, LV_STATE_CHECKED);
    }

    // 底部按钮：Cancel / Save（挂滑动防误触 + 滑动后短暂抑制点击）
    _btn_cancel = std::make_unique<Button>(_panel->get());
    _btn_cancel->setSize(100, 36);
    _btn_cancel->align(LV_ALIGN_BOTTOM_LEFT, 28, -8);
    apply_button_common_style(*_btn_cancel);
    _btn_cancel->label().setText("Cancel");
    _btn_cancel->onClick().connect([this]() {
        if (GetHAL().millis() - g_ctx.last_swipe_ms < kSwipeSuppressMs) {
            return;  // 滑动刚结束，算误触
        }
        _cancel_flag = true;
    });
    attach_swipe_guard(_btn_cancel->get(), false);

    _btn_save = std::make_unique<Button>(_panel->get());
    _btn_save->setSize(100, 36);
    _btn_save->align(LV_ALIGN_BOTTOM_RIGHT, -28, -8);
    apply_button_common_style(*_btn_save);
    _btn_save->label().setText("Save");
    _btn_save->onClick().connect([this]() {
        if (GetHAL().millis() - g_ctx.last_swipe_ms < kSwipeSuppressMs) {
            return;  // 滑动刚结束，算误触
        }
        _save_flag = true;
    });
    attach_swipe_guard(_btn_save->get(), false);

    // 底部留白：让面板可滚动范围变大（键盘弹出后能滚出底部按钮）
    lv_obj_t* spacer = lv_obj_create(_panel->get());
    lv_obj_set_size(spacer, 320, kExtraScrollHeight);
    lv_obj_set_pos(spacer, 0, 236);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_radius(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);

    // 虚拟键盘（lvgl_cpp 无 Keyboard wrapper，用裸 LVGL API）
    _keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_width(_keyboard, 320);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    g_ctx.keyboard = _keyboard;
    lv_obj_add_event_cb(_keyboard, on_keyboard_ready, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(_keyboard, on_keyboard_cancel, LV_EVENT_CANCEL, nullptr);
}

MibaoUrlWorker::~MibaoUrlWorker()
{
    mclog::tagInfo(_tag, "destroy");

    g_ctx = UrlContext{};  // 先清状态，防止悬垂指针

    if (_keyboard != nullptr) {
        lv_obj_del(_keyboard);
        _keyboard = nullptr;
    }
    _btn_save.reset();
    _btn_cancel.reset();
    if (_switch_ai != nullptr) {
        lv_obj_del(_switch_ai);
        _switch_ai = nullptr;
    }
    _label_ai_title.reset();
    _ta_iot.reset();
    _label_iot_title.reset();
    _ta_upload.reset();
    _label_upload_title.reset();
    _ta_ota.reset();
    _label_ota_title.reset();
    _label_title.reset();
    _panel.reset();
}

void MibaoUrlWorker::update()
{
    if (_save_flag) {
        const std::string ota = lv_textarea_get_text(_ta_ota->get());
        mibao::setOtaUrl(ota);
        // ota_url 改了必须同步到 xiaozhi 的 wifi 命名空间，否则 AI 对话 OTA 激活用不到。
        mibao::syncOtaUrlToXiaozhi();
        mibao::setUploadUrl(lv_textarea_get_text(_ta_upload->get()));
        mibao::setIotUrl(lv_textarea_get_text(_ta_iot->get()));
        mibao::setAiChatEnabled(lv_obj_has_state(_switch_ai, LV_STATE_CHECKED));
        mclog::tagInfo(_tag, "saved: ota={}, upload={}, iot={}, ai_chat={}", mibao::getOtaUrl(),
                       mibao::getUploadUrl(), mibao::getIotUrl(), mibao::isAiChatEnabled());
        _is_done = true;
        return;
    }

    if (_cancel_flag) {
        _is_done = true;
    }
}
