#!/usr/bin/env python3
"""Writes include/menu/mm_sizes.h: the size of each of the Mod Manager's pictures (port/tbo/ui/mm_*.png, in the order
include/menu/tbo_art.h lists them) and the corner of each that keeps its shape when the picture is stretched (a
plate's rounded ends, a window's frame; 0 for a picture drawn whole). The sizes are read from the PNG files; the
corners are the pictures' design and are kept here.

    python3 port/tools/mm_sizes.py           writes the header
    python3 port/tools/mm_sizes.py --check   says whether the header is what the pictures give (exit 1 if not)
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# name, corner (pixels of the picture)
PICTURES = [
    ("mm_bg", 0), ("mm_bar", 54), ("mm_bar_sel", 54), ("mm_tab", 46), ("mm_tab_sel", 46), ("mm_bar_grey", 41),
    ("mm_navbar", 44), ("mm_field", 44), ("mm_key", 30), ("mm_window_dark", 44), ("mm_card", 56), ("mm_window", 48),
    ("mm_emblem", 0), ("mm_platform", 0), ("mm_folder", 0), ("mm_folder_mod", 0), ("mm_folder_batch", 0),
    ("mm_zip", 0), ("mm_keycap", 34), ("mm_keycap_sel", 34), ("mm_cell", 30), ("mm_up", 0), ("mm_down", 0),
    ("mm_left", 0), ("mm_right", 0), ("mm_ball", 0), ("mm_lock", 0), ("mm_pip", 0), ("mm_pip_off", 0),
    ("mm_bg_dusk", 0),
]


def png_size(path):
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit(f"{path}: not a PNG file")
    return struct.unpack(">II", data[16:24])


def header():
    lines = ["/* Written by port/tools/mm_sizes.py: the Mod Manager's pictures (port/tbo/ui/mm_*.png, listed in tbo_art.h",
             "   after Tenkaichi Budokai Online's): width, height, the corner that keeps its shape (pixels of the picture). */",
             "#ifndef MM_SIZES_H", "#define MM_SIZES_H", "static const u16 sMmArtSize[][3] = {"]
    for name, corner in PICTURES:
        w, h = png_size(ROOT / "port/tbo/ui" / (name + ".png"))
        lines.append(f"    {{{w}, {h}, {corner}}}, /* {name} */")
    lines += ["};", "#endif", ""]
    return "\n".join(lines)


def main():
    out = ROOT / "include/menu/mm_sizes.h"
    text = header()
    if "--check" in sys.argv[1:]:
        same = out.exists() and out.read_text() == text
        print("mm_sizes.h is up to date" if same else "mm_sizes.h differs from the pictures: run port/tools/mm_sizes.py")
        sys.exit(0 if same else 1)
    out.write_text(text)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
