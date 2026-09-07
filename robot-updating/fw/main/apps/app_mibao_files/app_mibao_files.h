/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include <lvgl.h>
#include <mooncake.h>
#include <string>
#include <vector>
#include <atomic>

/**
 * @brief 本地文件管理 app（#8）
 *
 * 列表查看 SD 卡上的录音文件（/sdcard/meetings 与 /sdcard/personal），
 * 支持播放 / 删除 / 重命名。全部为本地操作，不依赖网络。
 */
class AppMibaoFiles : public mooncake::AppAbility {
public:
    AppMibaoFiles();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _list = nullptr;
    lv_obj_t* _title_label = nullptr;       // 顶部标题（首页"文件管理"/目录名）
    lv_obj_t* _title_count_label = nullptr;  // 顶部标题右侧"共 X 个文件"
    lv_obj_t* _back_btn = nullptr;  // 返回上一级（文件夹内显示）
    lv_obj_t* _btn_play = nullptr;
    lv_obj_t* _btn_delete = nullptr;
    lv_obj_t* _btn_rename = nullptr;
    lv_obj_t* _btn_convert = nullptr;  // 「转纪要」：批量转录音/视频为纪要
    lv_obj_t* _btn_play_label = nullptr;
    lv_obj_t* _btn_delete_label = nullptr;
    lv_obj_t* _btn_rename_label = nullptr;

    // 层级浏览状态
    std::string _browsing_dir;   // 当前浏览的目录（空 = 首页显示文件夹）
    std::vector<std::string> _folders;  // 首页显示的文件夹名（/sdcard 下子目录）
    bool _renaming_folder = false;  // 重命名对话框当前操作的是文件夹

    // 批量转纪要状态
    std::atomic<bool> _convert_running{false};  // 转换进行中
    std::atomic<bool> _convert_done_refresh{false};  // 转换完成需刷新列表
    std::string _convert_pending_path;  // 当前选中的待转换文件
    std::string _convert_pending_name;
    std::string _convert_pending_type;  // meeting / personal

    // 对话框（重命名用）
    lv_obj_t* _dialog = nullptr;
    lv_obj_t* _dialog_textarea = nullptr;
    lv_obj_t* _dialog_keyboard = nullptr;
    std::string _rename_old_path;
    std::string _rename_old_name;

    // 删除确认弹窗（全屏遮罩 + 对话框）
    lv_obj_t* _confirm_panel = nullptr;
    lv_obj_t* _confirm_label = nullptr;
    lv_obj_t* _confirm_ok = nullptr;
    lv_obj_t* _confirm_cancel = nullptr;

    std::vector<std::string> _files;  // 完整路径
    std::vector<std::string> _labels; // 显示名（含来源目录前缀）
    std::vector<bool> _row_is_header; // 该行是否为分组标题（不可选中）
    int _selected_index = -1;
    unsigned _wav_count = 0;  // 录音文件数
    unsigned _avi_count = 0;  // 视频文件数

    // 播放状态
    bool _playing = false;
    bool _stop_play = false;

    // 关闭请求（home indicator 触发，onRunning 检测后调 close()）
    bool _close_requested = false;

    void scanFiles();
    void refreshList();
    void refreshStatus();
    void setSelected(int index);
    void syncButtons();
    void enterFolder(const std::string& dir);
    void goBack();

    void startConvertAll();
    void convertAllTask();

    void playSelected();
    void deleteSelected();
    void openRenameDialog();
    void showDeleteConfirm();
    void hideDeleteConfirm();
    void doDeleteConfirmed();

    void createUi();
    void destroyUi();
    void destroyDialog();
};