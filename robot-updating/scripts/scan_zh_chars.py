#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
扫描 main/ 目录下所有 C/C++ 字符串字面量里的中文字符，
对照 main/assets/fonts/symbols.txt 输出缺失列表。

用法：
    python3 scripts/scan_zh_chars.py
    python3 scripts/scan_zh_chars.py --strict   # CI 模式：缺字时 exit 1

背景：
    LVGL 9 字体子集只包含 symbols.txt 显式列出的字符。
    新增文案若忘了补子集，运行时该字渲染为透明 → 表现为"文字截断"。

历史教训（2026-08-07）：
    "文件管理"只显示"文件理"——symbols.txt 缺"管"。
    "已删除"只显示"已"——symbols.txt 缺"删" "除"。
    修复：扩展 symbols.txt 至 ~1100 字符。
"""

import argparse
import os
import re
import sys
from pathlib import Path

# 字符串字面量匹配（容忍转义双引号）
STR_PATTERN = re.compile(r'"((?:[^"\\]|\\.)*)"')

# 注释行 / 块注释前缀
COMMENT_PREFIXES = ("//", "/*", "*", "///")


def collect_zh_from_source(root_dir: Path) -> set:
    """扫描 main/ 下所有 C/C++ 字符串字面量中的中文字符。"""
    chars = set()
    exts = {".cpp", ".cc", ".c", ".h", ".hpp"}
    for root, dirs, files in os.walk(root_dir):
        # 排除构建目录
        dirs[:] = [d for d in dirs if d not in ("build", "managed_components", "esp-idf-env", ".git")]
        for fn in files:
            if Path(fn).suffix not in exts:
                continue
            path = Path(root) / fn
            try:
                with open(path, "r", encoding="utf-8") as f:
                    for line in f:
                        stripped = line.strip()
                        if stripped.startswith(COMMENT_PREFIXES):
                            continue
                        for m in STR_PATTERN.findall(line):
                            for ch in m:
                                if "\u4e00" <= ch <= "\u9fff":
                                    chars.add(ch)
            except (OSError, UnicodeDecodeError) as e:
                print(f"  warn: skip {path}: {e}", file=sys.stderr)
    return chars


def collect_zh_from_html(root_dir: Path) -> set:
    """扫描 managed_components 里改过的 WiFi 配网页面（含 HTML 字面量）。"""
    chars = set()
    for root, dirs, files in os.walk(root_dir):
        for fn in files:
            if not fn.endswith((".cc", ".cpp", ".h")):
                continue
            path = Path(root) / fn
            try:
                with open(path, "r", encoding="utf-8") as f:
                    content = f.read()
                # 字符串里
                for m in STR_PATTERN.findall(content):
                    for ch in m:
                        if "\u4e00" <= ch <= "\u9fff":
                            chars.add(ch)
                # 裸字符（HTML 模板）
                for ch in content:
                    if "\u4e00" <= ch <= "\u9fff":
                        chars.add(ch)
            except (OSError, UnicodeDecodeError):
                pass
    return chars


def main() -> int:
    parser = argparse.ArgumentParser(description="检查字体子集覆盖度")
    parser.add_argument(
        "--fw-dir",
        default=str(Path(__file__).parent.parent / "fw"),
        help="固件根目录（默认：脚本同级的 fw/）",
    )
    parser.add_argument(
        "--symbols",
        default=None,
        help="symbols.txt 路径（默认 fw/main/assets/fonts/symbols.txt）",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="CI 模式：缺字时返回非零退出码",
    )
    args = parser.parse_args()

    fw_dir = Path(args.fw_dir).resolve()
    symbols_path = Path(args.symbols) if args.symbols else (fw_dir / "main" / "assets" / "fonts" / "symbols.txt")
    main_dir = fw_dir / "main"
    wifi_dir = fw_dir / "managed_components" / "78__esp-wifi-connect"

    if not symbols_path.exists():
        print(f"err: {symbols_path} 不存在", file=sys.stderr)
        return 2
    if not main_dir.exists():
        print(f"err: {main_dir} 不存在", file=sys.stderr)
        return 2

    print(f"[scan] main dir     = {main_dir}")
    print(f"[scan] wifi dir     = {wifi_dir}")
    print(f"[scan] symbols      = {symbols_path}")

    main_chars = collect_zh_from_source(main_dir)
    wifi_chars = collect_zh_from_html(wifi_dir) if wifi_dir.exists() else set()
    all_used = main_chars | wifi_chars

    with open(symbols_path, "r", encoding="utf-8") as f:
        symbols = set(f.read().strip())

    missing = all_used - symbols
    unused = symbols - all_used  # 仅供参考

    print(f"\n[result] main 用了 {len(main_chars)} 中文字符")
    print(f"[result] wifi html 用了 {len(wifi_chars)} 中文字符")
    print(f"[result] symbols.txt 包含 {len(symbols)} 中文字符")
    print(f"[result] 缺字: {len(missing)} 个")
    if missing:
        print(f"  missing: {''.join(sorted(missing))}")
    if unused:
        print(f"[result] 未用: {len(unused)} 个（可清理以省 flash，但默认不删）")

    if missing:
        print("\n[action] 把以上 missing 字追加到 symbols.txt，然后重新生成字体：")
        print("  cd <fw-dir>")
        print('  lv_font_conv --no-compress --no-prefilter --force-fast-kern-format \\')
        print("    --font managed_components/78__xiaozhi-fonts/ttf/puhui-common.ttf \\")
        print("    --format lvgl --lv-include lvgl.h --bpp 4 --size 16 \\")
        print("    --range 0x20-0x7e \\")
        print('    --symbols "$(cat main/assets/fonts/symbols.txt | tr -d \'\\n\')" \\')
        print("    --output main/assets/fonts/mibao_zh_font_16.c")
        if args.strict:
            return 1
    else:
        print("\n[ok] 字体子集完整，无需补充。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
