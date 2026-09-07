#!/usr/bin/env python3
"""
米宝一号 - PNG → LVGL 9 .bin 转换工具

LVGL 9 image header（lv_image_header_t，12 bytes，小端；见 lvgl/src/draw/lv_image_dsc.h）:
  - magic   (1 byte)  = 0x19 (LV_IMAGE_HEADER_MAGIC)
  - cf      (1 byte)  = LV_COLOR_FORMAT_ARGB8888（本项目 LVGL 枚举值 = 0x10，
                        以 managed_components/lvgl__lvgl/src/misc/lv_color.h 为准，
                        注意：不是 0x04 也不是 0x06）
  - flags   (2 bytes) = 0
  - w       (2 bytes)
  - h       (2 bytes)
  - stride  (2 bytes) = w * 4 (for ARGB8888)
  - reserved(2 bytes) = 0

输出格式: ARGB8888 (4 bytes per pixel)，保留 alpha 通道。
像素字节序为 LVGL 9 lv_color32_t 内存布局：B G R A（不是 A R G B）！
源 PNG 是 RGB 的自动补 alpha=255；RGBA 的直接保留。

坑记录（2026-08-06 屏保黑屏根因）：
旧版脚本把 magic 当 4 字节 0x19E70019 打包，导致 cf 字段被挤到 flags 位置、
LVGL 解析出 cf=0x00(UNKNOWN) + w=6，图片完全不渲染 → 屏保黑屏。

为什么不用 RGB565A8 / 220×220？
- 240×240 屏幕上 220×220 会被裁掉 → 项目约定屏保/大图 ≤ 150×150
- ARGB8888 保留 alpha 通道，屏保背景设黑色，米宝图透出来；RGB565A8 像素位深更浅，但屏保场景 alpha 完整更重要
- 屏保 2 张图 150×150 ARGB8888 = 90KB×2 = 180KB，比 RGB565A8 220×220 145KB×2 = 290KB 更省
"""

import os
import sys
import struct
from PIL import Image

# LVGL 9 image magic（仅 1 字节）+ ARGB8888 颜色格式
# 枚举值取自本项目实际使用的 LVGL 9.4.0（managed_components/lvgl__lvgl）：
# src/misc/lv_color.h 中 LV_COLOR_FORMAT_ARGB8888 = 0x10
LV_IMAGE_HEADER_MAGIC = 0x19
LV_COLOR_FORMAT_ARGB8888 = 0x10


def png_to_argb8888_bin(png_path: str, bin_path: str, target_size: tuple) -> int:
    """将 PNG 转为 LVGL 9 ARGB8888 .bin。

    Args:
        png_path: 输入 PNG 路径
        bin_path: 输出 .bin 路径
        target_size: (w, h)，统一缩放

    Returns:
        输出文件大小（bytes）
    """
    img = Image.open(png_path)
    src_mode = img.mode
    src_size = img.size

    # 缩放（LANCZOS 高质量）
    if img.size != target_size:
        img = img.resize(target_size, Image.LANCZOS)

    # RGB → 补 alpha=255
    if img.mode != "RGBA":
        img = img.convert("RGBA")

    w, h = img.size

    # RGBA → LVGL 9 lv_color32_t 内存布局：B G R A（小端下 uint32 为 0xAARRGGBB）
    pixels = list(img.getdata())
    argb = bytearray()
    for r, g, b, a in pixels:
        argb.append(b)
        argb.append(g)
        argb.append(r)
        argb.append(a)

    # 12 字节 header（magic/cf 各 1 字节，flags 2 字节，与 lv_image_header_t 位域一致）
    header = struct.pack(
        "<BBHHHHH",
        LV_IMAGE_HEADER_MAGIC,  # magic (1 byte)
        LV_COLOR_FORMAT_ARGB8888,  # cf (1 byte) = 0x10
        0,                # flags (2 bytes)
        w,
        h,
        w * 4,            # stride = w * 4 bytes/px
        0,                # reserved_2 (2 bytes)
    )
    assert len(header) == 12, f"header should be 12 bytes, got {len(header)}"

    with open(bin_path, "wb") as f:
        f.write(header)
        f.write(argb)

    size = 12 + len(argb)
    print(f"  {os.path.basename(png_path):30s} {src_size[0]:>4d}x{src_size[1]:<4d} {src_mode:5s} → "
          f"{target_size[0]:>3d}x{target_size[1]:<3d} ARGB8888 → "
          f"{os.path.basename(bin_path):35s} {size:>7d} B ({size/1024:.1f} KB)")
    return size


def main() -> int:
    src_dir = "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/main/assets/mibao"
    out_dir = "/Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw/main/assets/mibao_bin"

    os.makedirs(out_dir, exist_ok=True)

    print("=" * 90)
    print("米宝一号 - PNG → LVGL 9 ARGB8888 .bin 转换")
    print("=" * 90)
    print()

    # 1. 四个 app 图标（150×150，launcher 滚动列表用）
    print("【1/4】App 图标 (150×150 ARGB8888) - launcher 用")
    # 对话 app 图标：阶段 1.3 新增（新资源名，不覆盖原生 icon）
    png_to_argb8888_bin(
        f"{src_dir}/米宝_对话.png",
        f"{out_dir}/mibao_chat_150.bin",
        (150, 150),
    )
    png_to_argb8888_bin(
        f"{src_dir}/mibao_meeting_150.png",
        f"{out_dir}/mibao_meeting_150.bin",
        (150, 150),
    )
    png_to_argb8888_bin(
        f"{src_dir}/mibao_personal_150.png",
        f"{out_dir}/mibao_personal_150.bin",
        (150, 150),
    )
    png_to_argb8888_bin(
        f"{src_dir}/mibao_iot_150.png",
        f"{out_dir}/mibao_iot_150.bin",
        (150, 150),
    )
    print()

    # 2. 屏保眨眼（150×150，缩自原图 1941×2160 / 1797×2000）
    print("【2/4】屏保眨眼 (150×150 ARGB8888) - screensaver 轮播")
    png_to_argb8888_bin(
        f"{src_dir}/看你_睁眼.png",
        f"{out_dir}/mibao_look_open_150.bin",
        (150, 150),
    )
    png_to_argb8888_bin(
        f"{src_dir}/看你_闭眼.png",
        f"{out_dir}/mibao_look_close_150.bin",
        (150, 150),
    )
    print()

    # 3. 唤醒后右下角小图（150×150，从 2048×2048 缩）
    print("【3/4】唤醒后米宝小图 (150×150 ARGB8888) - wake UI 右下角")
    png_to_argb8888_bin(
        f"{src_dir}/认真工作.png",
        f"{out_dir}/mibao_work_150.bin",
        (150, 150),
    )
    png_to_argb8888_bin(
        f"{src_dir}/mibao_chat_60.png",
        f"{out_dir}/mibao_chat_60.bin",
        (60, 60),
    )
    print()

    # 4. 同步 mibao_bin/*.bin 到 assets_bin/（与 CMake file(COPY) 逻辑一致，
    #    保证不改 CMakeLists 也能让增量构建拿到最新资源）
    print("【4/4】同步 bin 到 assets_bin/")
    import shutil
    assets_bin_dir = os.path.join(os.path.dirname(out_dir), "assets_bin")
    for f in sorted(os.listdir(out_dir)):
        if f.endswith(".bin"):
            shutil.copy2(os.path.join(out_dir, f), os.path.join(assets_bin_dir, f))
            # CMake file(COPY) 历史产物：assets_bin/mibao_bin/ 嵌套副本，
            # 打包时后处理的会覆盖先处理的，两处都必须同步
            nested_dir = os.path.join(assets_bin_dir, "mibao_bin")
            if os.path.isdir(nested_dir):
                shutil.copy2(os.path.join(out_dir, f), os.path.join(nested_dir, f))
            print(f"  synced: {f}")
    print()

    # 汇总
    print("=" * 90)
    print(f"输出目录: {out_dir}")
    print(f"文件总数: {len([f for f in os.listdir(out_dir) if f.endswith('.bin')])}")
    total_size = sum(
        os.path.getsize(os.path.join(out_dir, f))
        for f in os.listdir(out_dir)
        if f.endswith(".bin")
    )
    print(f"总大小:   {total_size} bytes ({total_size/1024:.1f} KB)")
    print("=" * 90)

    return 0


if __name__ == "__main__":
    sys.exit(main())
