#!/usr/bin/env python3
"""
米宝一号 - 白底 PNG 背景透明化预处理

用途：
    部分源图（如 看你_闭眼.png）是 RGB 无 alpha 的实心白底图，直接转 ARGB8888 bin
    后屏保背景会显示成白色块。本脚本用"四角 flood-fill"把连通的白色背景置为透明，
    输出 RGBA PNG 覆盖源图，之后由 png_to_mibao_bin.py 正常转换。

方法：
    - 从图像四角（及四条边的边缘点）播种，BFS flood-fill；
    - 像素判为背景的条件：三通道均 >= WHITE_MIN（近白），且与父像素各通道差
      <= NEIGHBOR_TOL（允许白底有轻微渐变/噪声，但不会跨越深色轮廓线）；
    - 米宝机身的白色区域被深色轮廓线/阴影包围，flood-fill 无法穿过轮廓侵入，
      因此不会误伤主体白色；
    - 只处理 RGB（无 alpha）的图；已带 alpha 的图跳过。

为什么不用全局阈值抠白：
    米宝机身本身有白色面板，全局阈值会把机身一并抠掉；flood-fill 只去除与
    画面边缘连通的背景区域。
"""

import os
import sys
from collections import deque

import numpy as np
from PIL import Image

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

WHITE_MIN = 228      # 近白阈值：三通道都 >= 该值才算"白"
NEIGHBOR_TOL = 16    # 与相邻已判背景像素的最大通道差（容纳白底渐变）

# 需要处理的文件（仅白底 RGB 图；已带 alpha 的 看你_睁眼.png / 认真工作.png 不处理）
TARGETS = [
    "mibao/看你_闭眼.png",
]


def remove_white_background(img: Image.Image) -> tuple:
    """对 RGB 图做四角 flood-fill 抠白底。

    Returns:
        (RGBA Image, background_ratio)
    """
    assert img.mode == "RGB", f"expect RGB, got {img.mode}"
    rgb = np.asarray(img, dtype=np.int16)
    h, w, _ = rgb.shape

    near_white = np.all(rgb >= WHITE_MIN, axis=2)  # bool HxW

    visited = np.zeros((h, w), dtype=bool)
    dq = deque()

    # 播种：四条边上的近白像素（比仅四角更稳，避免背景在边角被主体切断）
    edge_pts = (
        [(x, 0) for x in range(w)]
        + [(x, h - 1) for x in range(w)]
        + [(0, y) for y in range(h)]
        + [(w - 1, y) for y in range(h)]
    )
    for x, y in edge_pts:
        if near_white[y, x]:
            visited[y, x] = True
            dq.append((x, y))

    # BFS flood-fill（含邻接容差，防止跨越深色轮廓）
    while dq:
        x, y = dq.popleft()
        cur = rgb[y, x]
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if nx < 0 or nx >= w or ny < 0 or ny >= h:
                continue
            if visited[ny, nx] or not near_white[ny, nx]:
                continue
            if np.max(np.abs(rgb[ny, nx] - cur)) > NEIGHBOR_TOL:
                continue
            visited[ny, nx] = True
            dq.append((nx, ny))

    alpha = np.where(visited, 0, 255).astype(np.uint8)
    rgba = np.dstack([rgb.astype(np.uint8), alpha])
    ratio = visited.sum() / (h * w)
    return Image.fromarray(rgba, "RGBA"), ratio


def main() -> int:
    print("=" * 80)
    print("米宝一号 - 白底 PNG 背景透明化（四角 flood-fill）")
    print(f"参数: WHITE_MIN={WHITE_MIN}, NEIGHBOR_TOL={NEIGHBOR_TOL}")
    print("=" * 80)

    for rel in TARGETS:
        path = os.path.join(BASE_DIR, rel)
        img = Image.open(path)
        if img.mode != "RGB":
            print(f"  {rel}: mode={img.mode}，已带 alpha，跳过")
            continue
        rgba, ratio = remove_white_background(img)
        rgba.save(path)
        print(f"  {rel}: {img.size[0]}x{img.size[1]} RGB → RGBA，"
              f"背景透明占比 {ratio*100:.1f}%，已覆盖保存")

        # 自检：四角 alpha 应为 0，中心区域 alpha 应为 255
        w, h = rgba.size
        corners = [rgba.getpixel(c)[3] for c in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]]
        center_a = rgba.getpixel((w // 2, h // 2))[3]
        print(f"    corner_alphas={corners} center_alpha={center_a}")
        assert all(a == 0 for a in corners), "四角应透明"
        assert center_a == 255, "中心主体应不透明"

    print("完成")
    return 0


if __name__ == "__main__":
    sys.exit(main())
