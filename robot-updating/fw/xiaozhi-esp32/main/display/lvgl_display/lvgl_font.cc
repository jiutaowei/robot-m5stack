#include "lvgl_font.h"
#include <cbin_font.h>

// 米宝：设备气泡文本用的是 assets 分区里的 font_puhui_common_20_4.bin
// （源自 puhui-common.ttf，只有 6658 个码点）。该字表缺少植保场景的高频字：
// 虱(U+8671)、螟(U+879F)、蚜、蝽、蛹、蝼、螬…… 于是「稻飞虱」在屏幕上
// 会显示成「稻飞」，「稻纵卷叶螟」显示成「稻纵卷叶」。
// 整份换字体放不下（assets 分区仅余约 0.68MB，换 noto 需 +1.68MB），
// 因此保留原字体，另挂一个只含这些缺字的小字体。
// LVGL 原生支持 fallback 链（lv_font.c: f = f->fallback），正常字仍由原
// 字体渲染，观感不变；只有缺失字会落到兜底字体。
// 兜底字体的生成方式见 robot-updating/scripts/gen_extra_font.py。
LV_FONT_DECLARE(mibao_cn_extra_20_4);

LvglCBinFont::LvglCBinFont(void* data) {
    font_ = cbin_font_create(static_cast<uint8_t*>(data));
    if (font_ != nullptr && font_->fallback == nullptr) {
        font_->fallback = &mibao_cn_extra_20_4;
    }
}

LvglCBinFont::~LvglCBinFont() {
    if (font_ != nullptr) {
        cbin_font_delete(font_);
    }
}