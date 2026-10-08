#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成米宝「生僻字兜底字体」，修复设备气泡缺字（如 稻飞虱 → 稻飞）。

背景
----
设备气泡文本用的字体来自 assets 分区，由 xiaozhi 的
`scripts/build_default_assets.py:get_text_font_path()` 决定：
    BUILTIN_TEXT_FONT=font_puhui_basic_20_4
      -> cbin/font_puhui_common_20_4.bin
      -> 源自 ttf/puhui-common.ttf（仅 6658 个码点）
该字表没有 虱(U+8671)/螟(U+879F)/蚜/蝽/蛹 等植保高频生僻字，而它又整份
替换不进（assets 分区只剩 ~0.68MB，换 noto 需 +1.68MB）。因此保留原字体，
另生成一个只含「缺失字」的小字体，在
xiaozhi-esp32/main/display/lvgl_display/lvgl_font.cc 的 LvglCBinFont 构造里
作为 LVGL `font->fallback` 挂到气泡字体上（LVGL 原生支持：lv_font.c 的 fallback 链）。

本脚本
------
1. 从词表 vocab.txt 拆出所有汉字；
2. 减去 primary-font(puhui-common.ttf) 已有的字（已能显示，重复收录纯浪费 flash）；
3. 校验剩下的字在 source-font(noto-qwen.ttf) 里都有字形；
4. 输出 symbols 清单，并调用 lv_font_conv 生成 mibao_cn_extra_20_4.c；
5. 回读生成的 .c，解析 cmaps 校验关键生僻字确实进了字体且字形非空。

依赖：lv_font_conv（`npm i -g lv_font_conv@1.5.3`）。

用法：
    python robot-updating/scripts/gen_extra_font.py                # 生成
    python robot-updating/scripts/gen_extra_font.py --check-only   # 只看统计，不生成
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys


def _find_repo_root(start: str) -> str:
    """从脚本所在目录向上找到含 robot-updating/ 的仓库根，便于目录搬迁。"""
    p = os.path.abspath(start)
    for _ in range(6):
        if os.path.isdir(os.path.join(p, "robot-updating")):
            return p
        parent = os.path.dirname(p)
        if parent == p:
            break
        p = parent
    return os.path.abspath(start)


REPO_ROOT = _find_repo_root(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FONTS_DIR = os.path.join(REPO_ROOT, "robot-updating", "fw", "main", "assets", "fonts")
DEFAULT_COMPONENT_TTF = os.path.join(
    REPO_ROOT, "robot-updating", "fw", "managed_components", "78__xiaozhi-fonts", "ttf"
)

# 必须覆盖的字：缺任何一个都说明这次修复没生效（就是用户报的那两个）
CRITICAL = "虱螟"


# --------------------------------------------------------------------------- #
# TTF cmap 解析（纯标准库，不依赖 fontTools）
# --------------------------------------------------------------------------- #
def ttf_codepoints(path: str) -> set[int]:
    data = open(path, "rb").read()
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables: dict[str, tuple[int, int]] = {}
    for i in range(num_tables):
        off = 12 + 16 * i
        tag = data[off:off + 4].decode("latin1")
        toff, tlen = struct.unpack(">II", data[off + 8:off + 16])
        tables[tag] = (toff, tlen)
    if "cmap" not in tables:
        raise ValueError(f"{path}: 没有 cmap 表")

    cmap_off = tables["cmap"][0]
    num_sub = struct.unpack(">H", data[cmap_off + 2:cmap_off + 4])[0]
    cps: set[int] = set()

    for i in range(num_sub):
        p = cmap_off + 4 + 8 * i
        _pid, _eid, sub_off = struct.unpack(">HHI", data[p:p + 8])
        so = cmap_off + sub_off
        fmt = struct.unpack(">H", data[so:so + 2])[0]

        if fmt == 4:
            seg_x2 = struct.unpack(">H", data[so + 6:so + 8])[0]
            seg = seg_x2 // 2
            end_o = so + 14
            start_o = end_o + seg_x2 + 2
            delta_o = start_o + seg_x2
            range_o = delta_o + seg_x2
            ends = struct.unpack(">%dH" % seg, data[end_o:end_o + seg_x2])
            starts = struct.unpack(">%dH" % seg, data[start_o:start_o + seg_x2])
            deltas = struct.unpack(">%dh" % seg, data[delta_o:delta_o + seg_x2])
            ranges = struct.unpack(">%dH" % seg, data[range_o:range_o + seg_x2])
            for k in range(seg):
                s, e = starts[k], ends[k]
                if s == 0xFFFF:
                    continue
                for c in range(s, e + 1):
                    if ranges[k] == 0:
                        gid = (c + deltas[k]) & 0xFFFF
                    else:
                        gi = range_o + k * 2 + ranges[k] + (c - s) * 2
                        gid = 0
                        if gi + 2 <= len(data):
                            gid = struct.unpack(">H", data[gi:gi + 2])[0]
                            if gid:
                                gid = (gid + deltas[k]) & 0xFFFF
                    if gid:
                        cps.add(c)
        elif fmt == 12:
            n_groups = struct.unpack(">I", data[so + 12:so + 16])[0]
            for k in range(n_groups):
                p2 = so + 16 + 12 * k
                sc, ec, _gid = struct.unpack(">III", data[p2:p2 + 12])
                for c in range(sc, ec + 1):
                    cps.add(c)
        elif fmt == 6:
            first, count = struct.unpack(">HH", data[so + 6:so + 10])
            for k in range(count):
                if struct.unpack(">H", data[so + 10 + 2 * k:so + 12 + 2 * k])[0]:
                    cps.add(first + k)
        elif fmt == 0:
            for c in range(256):
                if data[so + 6 + c]:
                    cps.add(c)
    return cps


# --------------------------------------------------------------------------- #
# 生成的 .c 回读校验
# --------------------------------------------------------------------------- #
def generated_c_coverage(path: str) -> set[int]:
    """解析 lv_font_conv 生成的 .c 里的 cmaps[]，按 cmap 类型得到真实覆盖码点。

    注意：`--symbols` 生成的是 SPARSE_TINY 表，此时 range_start/range_length 只是
    [最小码点, 最大码点] 的跨度（可能上万），真正有字形的码点列在 unicode_list 里。
    早期版本按 range 展开会得到 17757 个「覆盖码点」的假象，必须避免。
    """
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise ValueError(f"{path}: 没找到 cmaps[] 数组")
    body = m.group(1)

    arrays: dict[str, list[int]] = {}
    for am in re.finditer(r"uint(?:8|16|32)_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        vals: list[int] = []
        for tok in re.findall(r"0x[0-9a-fA-F]+|\d+", am.group(2)):
            vals.append(int(tok, 0))
        arrays[am.group(1)] = vals

    covered: set[int] = set()
    for em in re.finditer(r"\{([^{}]*)\}", body):
        e = em.group(1)
        rs = int(re.search(r"\.range_start\s*=\s*(\d+)", e).group(1))
        rl = int(re.search(r"\.range_length\s*=\s*(\d+)", e).group(1))
        typ = (re.search(r"\.type\s*=\s*(\w+)", e) or [None, ""])[1]
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", e)
        go = re.search(r"\.glyph_id_ofs_list\s*=\s*(\w+)", e)

        if "SPARSE" in typ and ul and ul.group(1) != "NULL":
            # 稀疏表：unicode_list 里存的是相对 range_start 的偏移（见 LVGL
            # lv_font_fmt_txt.c: rcp = letter - range_start，再对 unicode_list 二分）
            covered.update(rs + v for v in arrays.get(ul.group(1), []))
        elif "FORMAT0" in typ and go and go.group(1) != "NULL":
            # 稠密表 + 偏移表：偏移为 0 表示该码点无字形（首字符除外）
            for i, v in enumerate(arrays.get(go.group(1), [])):
                if v or (rs + i) == rs:
                    covered.add(rs + i)
        else:
            covered.update(range(rs, rs + rl))
    return covered


def glyph_id_of(path: str, cp: int) -> int:
    """按生成的 .c 里的 cmap 反推某码点的 glyph id（用于查字形是否为空）。"""
    text = open(path, encoding="utf-8", errors="replace").read()
    arrays: dict[str, list[int]] = {}
    for am in re.finditer(r"uint(?:8|16|32)_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        arrays[am.group(1)] = [int(t, 0) for t in re.findall(r"0x[0-9a-fA-F]+|\d+", am.group(2))]
    m = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    for em in re.finditer(r"\{([^{}]*)\}", m.group(1)):
        e = em.group(1)
        rs = int(re.search(r"\.range_start\s*=\s*(\d+)", e).group(1))
        gid0 = int(re.search(r"\.glyph_id_start\s*=\s*(\d+)", e).group(1))
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", e)
        if not ul or ul.group(1) == "NULL":
            continue
        for i, ofs in enumerate(arrays.get(ul.group(1), [])):
            if rs + ofs == cp:
                return gid0 + i
    return -1


def glyph_boxes(path: str) -> list[tuple[int, int]]:
    """返回 glyph_dsc 里每个字形的 (box_w, box_h)，下标即 glyph id。"""
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return []
    return [(int(w), int(h)) for w, h in
            re.findall(r"\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+)", m.group(1))]


def count_glyphs(path: str) -> int:
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return -1
    return len(re.findall(r"\.bitmap_index\s*=", m.group(1)))


# --------------------------------------------------------------------------- #
def is_cjk(ch: str) -> bool:
    o = ord(ch)
    return (
        0x3400 <= o <= 0x4DBF      # 扩展 A
        or 0x4E00 <= o <= 0x9FFF   # 基本区
        or 0xF900 <= o <= 0xFAFF   # 兼容汉字
        or 0x20000 <= o <= 0x2FA1F # 扩展 B~F
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="生成米宝生僻字兜底字体")
    ap.add_argument("--vocab", default=os.path.join(DEFAULT_FONTS_DIR, "mibao_cn_extra_vocab.txt"))
    ap.add_argument("--out", default=os.path.join(DEFAULT_FONTS_DIR, "mibao_cn_extra_20_4.c"))
    ap.add_argument("--symbols-out", default=os.path.join(DEFAULT_FONTS_DIR, "mibao_cn_extra_symbols.txt"))
    ap.add_argument("--primary-font", default=os.path.join(DEFAULT_COMPONENT_TTF, "puhui-common.ttf"),
                    help="气泡实际使用字体的源 TTF（puhui-common.ttf）")
    ap.add_argument("--source-font", default=os.path.join(DEFAULT_COMPONENT_TTF, "noto-qwen.ttf"),
                    help="生僻字字形来源 TTF（noto-qwen.ttf）")
    ap.add_argument("--size", type=int, default=20, help="与气泡字体同尺寸")
    ap.add_argument("--bpp", type=int, default=4)
    ap.add_argument("--lv-font-conv", default=None)
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()

    for p in (args.vocab, args.primary_font, args.source_font):
        if not os.path.exists(p):
            print(f"[x] 文件不存在: {p}", file=sys.stderr)
            return 2

    vocab_text = open(args.vocab, encoding="utf-8").read()
    vocab_chars = {ch for line in vocab_text.splitlines() if not line.startswith("#")
                   for ch in line if is_cjk(ch)}

    have = ttf_codepoints(args.primary_font)
    avail = ttf_codepoints(args.source_font)

    needed = sorted(c for c in vocab_chars if ord(c) not in have)
    no_glyph = [c for c in needed if ord(c) not in avail]
    emit = [c for c in needed if ord(c) in avail]

    print(f"词表汉字           : {len(vocab_chars)}")
    print(f"气泡字体现有       : {len(have)}  (puhui-common.ttf)")
    print(f"其中已能显示       : {len(vocab_chars) - len(needed)}")
    print(f"需要兜底的字       : {len(needed)}")
    print(f"源字体也无字形     : {len(no_glyph)} {''.join(no_glyph) if no_glyph else ''}")
    print(f"将写入兜底字体     : {len(emit)}  {''.join(emit[:40])}{'...' if len(emit) > 40 else ''}")

    # 关键校验：虱/螟 必须属于「需要兜底」这个集合，否则说明前提判断错了
    bad = [c for c in CRITICAL if c not in needed]
    if bad:
        print(f"[!] 关键生僻字 {''.join(bad)} 并未缺失于气泡字体，请重新核对结论", file=sys.stderr)
        return 3
    bad2 = [c for c in CRITICAL if ord(c) not in avail]
    if bad2:
        print(f"[x] 源字体 {args.source_font} 也缺 {' '.join(bad2)}，请换源字体", file=sys.stderr)
        return 3

    open(args.symbols_out, "w", encoding="utf-8").write("".join(emit) + "\n")
    print(f"符号清单已写入     : {os.path.relpath(args.symbols_out, REPO_ROOT)}")

    if args.check_only:
        return 0

    exe = args.lv_font_conv or shutil.which("lv_font_conv") or shutil.which("lv_font_conv.cmd")
    if not exe:
        print("[x] 找不到 lv_font_conv，请先 `npm i -g lv_font_conv@1.5.3`", file=sys.stderr)
        return 4

    cmd = [
        exe,
        "--no-compress", "--no-prefilter", "--force-fast-kern-format",
        "--font", args.source_font,
        "--format", "lvgl", "--lv-include", "lvgl.h",
        "--bpp", str(args.bpp), "--size", str(args.size),
        "--symbols", "".join(emit),
        "--output", args.out,
    ]
    print("\n$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd[:6]) + f' ... --symbols[{len(emit)}字] --output {os.path.basename(args.out)}')
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout.strip():
        print(proc.stdout.strip())
    if proc.returncode != 0:
        print(proc.stderr.strip(), file=sys.stderr)
        print(f"[x] lv_font_conv 失败，exit={proc.returncode}", file=sys.stderr)
        return 5
    warn = [l for l in (proc.stdout + proc.stderr).splitlines() if "not found" in l.lower() or "warn" in l.lower()]
    if warn:
        print("lv_font_conv 警告:")
        for l in warn[:20]:
            print("   " + l)

    covered = generated_c_coverage(args.out)
    emit_cps = {ord(c) for c in emit}
    lost = [c for c in emit if ord(c) not in covered]
    extra = sorted(covered - emit_cps)
    still = [c for c in CRITICAL if ord(c) not in covered]
    size_kb = os.path.getsize(args.out) / 1024
    print(f"\n生成: {os.path.relpath(args.out, REPO_ROOT)}  {size_kb:.1f} KB  "
          f"字形数={count_glyphs(args.out)}  实际覆盖码点={len(covered)}")
    if extra:
        print(f"     额外码点(非预期): {len(extra)} 个 {extra[:20]}")
    if lost:
        print(f"[x] 以下字未进入生成结果: {''.join(lost)}", file=sys.stderr)
        return 6
    if still:
        print(f"[x] 生成结果里仍缺 {' '.join(still)}", file=sys.stderr)
        return 6

    # 字形位图非空校验：cmap 里列了码点，也可能是空白字形（box_w/box_h = 0）
    boxes = glyph_boxes(args.out)
    blank = []
    for c in emit:
        gid = glyph_id_of(args.out, ord(c))
        if gid < 0 or gid >= len(boxes) or boxes[gid][0] == 0 or boxes[gid][1] == 0:
            blank.append(c)
    if blank:
        print(f"[x] 以下字的字形为空(位图为 0): {''.join(blank)}", file=sys.stderr)
        return 7
    sample = [f"{c}({boxes[glyph_id_of(args.out, ord(c))][0]}x{boxes[glyph_id_of(args.out, ord(c))][1]})"
              for c in "虱螟"]
    print(f"字形位图非空校验    : {len(emit)}/{len(emit)} 通过   例: {' '.join(sample)}")
    print(f"关键生僻字校验通过  : {' '.join(CRITICAL)} 已在字体中（{len(emit)}/{len(emit)} 全部命中）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
