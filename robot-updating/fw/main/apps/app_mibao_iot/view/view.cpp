/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <mooncake.h>
#include <lvgl.h>
#include <hal/hal.h>
#include <assets/assets.h>
#include <apps/common/toast/toast.h>
#include <smooth_ui_toolkit.hpp>
#include <smooth_lvgl.hpp>
#include <cJSON.h>
#include <memory>
#include <atomic>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace smooth_ui_toolkit::lvgl_cpp;

// 物联网页统一用 16px 米宝中文字体（原 26px 在小按键/顶栏中过大，
// 且标题「物联网控制」26px 会与返回键挤在一起）。
LV_FONT_DECLARE(mibao_zh_font_16);

namespace MibaoIotView {

// 米宝主题色（与 app_meeting 一致）
static constexpr uint32_t kAccentColor    = 0x2DBE8D;
static constexpr uint32_t kBgColor        = 0x101417;
static constexpr uint32_t kPanelColor     = 0x1C2428;
static constexpr uint32_t kInkColor       = 0x273238;
static constexpr uint32_t kTextColor      = 0xF2F7F4;
static constexpr uint32_t kMutedTextColor = 0x9FAAA5;

static constexpr int kTileCount = 4;

struct TileDef {
    const char* label;
    const char* actuator_id;
};

static constexpr TileDef kTiles[kTileCount] = {
    {"风扇", "fan"},
    {"水泵", "pump"},
    {"生长灯", "light"},
    {"加热垫", "heat"},
};

static std::unique_ptr<Container> _root;
static std::unique_ptr<Container> _back_btn;
static std::unique_ptr<Label> _back_label;
static std::unique_ptr<Label> _title;
static std::unique_ptr<Container> _tile_panels[kTileCount];
static std::unique_ptr<Label> _tile_labels[kTileCount];

static std::function<void()> _on_back;
static bool _tile_on[kTileCount] = {false, false, false, false};
static bool _busy                = false;

/* -------------------------------------------------------------------------- */
/*                        异步控制请求（卸载阻塞 HTTP）                        */
/* -------------------------------------------------------------------------- */
// 此前点击回调在 LVGL 任务里同步跑 esp_http_client POST（5s 超时）+ cJSON +
// fmt，调用栈很深，会压爆 LVGL port 任务栈（默认 ~6KB）触发 panic 重启。
// 现在把 HTTP 放到独立的大栈任务，LVGL 侧只负责发请求与轮询结果。
struct ControlReq {
    int index;
    std::string actuator_id;
    std::string action;
};

static std::atomic<bool> _task_running{false};
static std::atomic<bool> _result_ready{false};
static int _result_index = -1;
static bool _result_ok   = false;

static void control_task(void* arg)
{
    auto* req = static_cast<ControlReq*>(arg);

    mclog::tagInfo("MibaoIotView", "control task start: {} -> {}", req->actuator_id, req->action);
    const std::string result = controlGreenhouseActuator(req->actuator_id, req->action, 0);
    mclog::tagInfo("MibaoIotView", "control task got result: {}", result);

    bool ok    = false;
    cJSON* json    = cJSON_Parse(result.c_str());
    cJSON* success = json ? cJSON_GetObjectItemCaseSensitive(json, "success") : nullptr;
    ok             = cJSON_IsTrue(success);
    cJSON_Delete(json);

    mclog::tagInfo("MibaoIotView", "control done: {} -> {}, ok={}", req->actuator_id, req->action, ok);

    _result_index = req->index;
    _result_ok    = ok;
    _result_ready.store(true);
    _task_running.store(false);

    delete req;
    vTaskDelete(nullptr);
}

/* -------------------------------------------------------------------------- */
/*                                   UI 辅助                                   */
/* -------------------------------------------------------------------------- */
static void apply_tile_visual(int index)
{
    if (!_tile_panels[index] || !_tile_labels[index]) {
        return;
    }
    if (_tile_on[index]) {
        _tile_panels[index]->setBorderWidth(2);
        _tile_panels[index]->setBorderColor(lv_color_hex(kAccentColor));
        _tile_labels[index]->setTextColor(lv_color_hex(kAccentColor));
    } else {
        _tile_panels[index]->setBorderWidth(0);
        _tile_labels[index]->setTextColor(lv_color_hex(kTextColor));
    }
}

static void set_tiles_enabled(bool enabled)
{
    for (int i = 0; i < kTileCount; ++i) {
        if (!_tile_panels[i]) {
            continue;
        }
        if (enabled) {
            _tile_panels[i]->addFlag(LV_OBJ_FLAG_CLICKABLE);
        } else {
            _tile_panels[i]->removeFlag(LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void handle_tile_clicked(int index)
{
    // 请求进行中或已有后台任务则忽略，避免并发/重复下发
    if (_busy || _task_running.load() || index < 0 || index >= kTileCount) {
        return;
    }

    // 防御：Wi-Fi 未连接时直接提示，避免 esp_http_client 在未初始化的网络栈上崩溃闪退（#7）
    if (GetHAL().getWifiStatus() == WifiStatus::None) {
        view::pop_a_toast("Wi-Fi 未连接", view::ToastType::Error);
        return;
    }

    _busy = true;

    const char* action = _tile_on[index] ? "off" : "on";

    // 点击立即 disable 并显示「控制中...」，后台任务返回后再刷新
    set_tiles_enabled(false);
    _tile_labels[index]->setText("控制中...");
    mclog::tagInfo("MibaoIotView", "control: {} -> {} (async)", kTiles[index].actuator_id, action);

    auto* req = new ControlReq{index, kTiles[index].actuator_id, action};
    _task_running.store(true);
    // 16KB 大栈，独立于 LVGL 任务，杜绝 esp_http_client + fmt + cJSON 栈溢出触发 panic 重启
    if (xTaskCreate(control_task, "iot_ctrl", 16384, req, 5, nullptr) != pdPASS) {
        mclog::tagError("MibaoIotView", "create control task failed");
        delete req;
        _task_running.store(false);
        _tile_labels[index]->setText(kTiles[index].label);
        set_tiles_enabled(true);
        _busy = false;
    }
}

static void create_back_button()
{
    // 返回按钮：顶栏内 (4, 32)，64×32，紧贴状态栏下方，不与网格重叠
    _back_btn = std::make_unique<Container>(_root->get());
    _back_btn->setSize(64, 32);
    _back_btn->align(LV_ALIGN_TOP_LEFT, 4, 32);
    _back_btn->setBgColor(lv_color_hex(0xE0E0E0));
    _back_btn->setRadius(8);
    _back_btn->setBorderWidth(0);
    _back_btn->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _back_btn->addFlag(LV_OBJ_FLAG_CLICKABLE);

    _back_label = std::make_unique<Label>(_back_btn->get());
    _back_label->setText(LV_SYMBOL_LEFT " 返回");
    _back_label->setTextColor(lv_color_hex(kInkColor));
    _back_label->setTextFont(&mibao_zh_font_16);
    _back_label->align(LV_ALIGN_CENTER, 0, 0);

    _back_btn->onClick().connect([]() {
        if (_on_back) {
            _on_back();
        }
    });
}

void build(std::function<void()> on_back)
{
    mclog::tagInfo("MibaoIotView", "build");

    _on_back = on_back;
    _busy    = false;
    _result_ready.store(false);
    _result_index = -1;
    _result_ok    = false;

    // 根容器
    _root = std::make_unique<Container>(lv_screen_active());
    _root->setSize(320, 240);
    _root->setAlign(LV_ALIGN_CENTER);
    _root->setBgColor(lv_color_hex(kBgColor));
    _root->setBorderWidth(0);
    _root->setRadius(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->setPadding(0, 0, 0, 0);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // 返回按钮（左上角，统一标准，不被网格遮挡）
    create_back_button();

    // 标题（16px，居中于顶栏）
    _title = std::make_unique<Label>(_root->get());
    _title->setText("物联网控制");
    _title->setTextColor(lv_color_hex(kTextColor));
    _title->setTextFont(&mibao_zh_font_16);
    _title->align(LV_ALIGN_TOP_MID, 0, 40);

    // 2×2 控制网格：风扇 / 水泵 / 生长灯 / 加热垫
    // 顶栏（状态栏 28 + 返回/标题 40）之下从 y=70 开始，避开返回按钮 (32-64)
    constexpr int tile_w = 140;
    constexpr int tile_h = 80;
    constexpr int gap    = 8;
    constexpr int left   = 16;
    constexpr int top    = 70;

    for (int i = 0; i < kTileCount; ++i) {
        const int col = i % 2;
        const int row = i / 2;

        _tile_panels[i] = std::make_unique<Container>(_root->get());
        _tile_panels[i]->setSize(tile_w, tile_h);
        _tile_panels[i]->align(LV_ALIGN_TOP_LEFT, left + col * (tile_w + gap), top + row * (tile_h + gap));
        _tile_panels[i]->setBgColor(lv_color_hex(kPanelColor));
        _tile_panels[i]->setRadius(10);
        _tile_panels[i]->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _tile_panels[i]->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _tile_panels[i]->addFlag(LV_OBJ_FLAG_CLICKABLE);

        _tile_labels[i] = std::make_unique<Label>(_tile_panels[i]->get());
        _tile_labels[i]->setText(kTiles[i].label);
        _tile_labels[i]->setTextFont(&mibao_zh_font_16);
        _tile_labels[i]->setTextColor(lv_color_hex(kTextColor));
        _tile_labels[i]->align(LV_ALIGN_CENTER, 0, 0);

        apply_tile_visual(i);

        _tile_panels[i]->onClick().connect([i]() { handle_tile_clicked(i); });
    }
}

void update()
{
    // 回收后台任务结果（在 LVGL 锁内由 app onRunning 驱动）
    if (_result_ready.load()) {
        _result_ready.store(false);

        const int index = _result_index;
        const bool ok   = _result_ok;

        if (index >= 0 && index < kTileCount && _tile_labels[index]) {
            _tile_labels[index]->setText(kTiles[index].label);
            if (ok) {
                _tile_on[index] = !_tile_on[index];
                view::pop_a_toast("控制成功", view::ToastType::Success);
            } else {
                view::pop_a_toast("控制失败", view::ToastType::Error);
            }
            apply_tile_visual(index);
        }

        set_tiles_enabled(true);
        _busy = false;
    }
}

void destroy()
{
    mclog::tagInfo("MibaoIotView", "destroy");
    for (int i = 0; i < kTileCount; ++i) {
        _tile_labels[i].reset();
        _tile_panels[i].reset();
        _tile_on[i] = false;
    }
    _title.reset();
    _back_label.reset();
    _back_btn.reset();
    _root.reset();
    _on_back = nullptr;
    _busy    = false;
    _result_ready.store(false);
}

}  // namespace MibaoIotView
