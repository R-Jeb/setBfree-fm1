#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The B3 ORGAN logo: a row of drawbars seen at an angle, some pulled out (brown, white and black knobs, the numbers on the
shafts), over the wordmark. Drawn here (no photo), 240 x 240 like the FM-1's screen.

  python3 tools/gen_logo.py            firmware/src/organ_logo.h (32 colours, RLE) and docs/logo.png, docs/logo@2.png

The font is Poppins (SIL Open Font License; fonts-poppins / Google Fonts); DejaVu Sans Bold if it is missing.
"""
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
S = 4                                            # drawn 4x, then scaled down (smooth edges)
W = 240 * S
BG = (62, 45, 32)                                 # a warm brown, not black
KNOB = {"b": ((110, 58, 30), (160, 96, 56), (60, 30, 14)),        # body, the lit top, the shadow
        "w": ((232, 226, 210), (255, 252, 240), (150, 144, 130)),
        "k": ((30, 28, 27), (120, 116, 110), (6, 6, 6))}
COLS = "bbwwkwkkw"                               # the drawbars' colours, 16' to 1'
PULL = [8, 8, 6, 8, 3, 0, 0, 2, 4]               # a registration: how far each one is out
SHAFT, SHAFT_LO, NUM = (232, 226, 210), (150, 144, 130), (20, 18, 16)


def font(names, size):
    for n in names:
        for d in ("/usr/share/fonts/truetype/google-fonts", "/usr/share/fonts/truetype/poppins", "/usr/share/fonts/truetype/dejavu",
                  os.path.expanduser("~/.fonts"), "/usr/share/fonts/TTF"):
            p = Path(d) / n
            if p.exists():
                return ImageFont.truetype(str(p), size)
    raise SystemExit("no font: install Poppins (fonts-poppins) or DejaVu")


BOLD = ("Poppins-Bold.ttf", "DejaVuSans-Bold.ttf")
MEDIUM = ("Poppins-Medium.ttf", "DejaVuSans.ttf")


def bars():
    """the drawbars upright: the panel at the top, the shafts down to the knobs"""
    c = Image.new("RGBA", (2400, 2000), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    py, pitch, sw, kw, step = 300, 200, 64, 150, 70
    d.rectangle([0, 0, 2400, py], fill=(26, 20, 16))
    d.line([0, py - 4, 2400, py - 4], fill=(150, 124, 96), width=8)
    nf = font(BOLD, 50)
    for i, (col, p) in enumerate(zip(COLS, PULL)):
        x = 250 + i * pitch
        ky = py + 40 + p * step
        if p:
            d.rectangle([x - sw // 2, py, x + sw // 2, ky + 10], fill=SHAFT)
            d.rectangle([x - sw // 2, py, x - sw // 2 + 8, ky + 10], fill=SHAFT_LO)
            for n in range(1, p + 1):                    # 1 next to the knob, the higher ones toward the panel
                y = ky - n * step + step // 2
                if y > py + 4:
                    d.text((x, y), str(n), font=nf, fill=NUM, anchor="mm")
        base, hi, lo = KNOB[col]
        k0, k1 = ky, ky + 230
        d.rounded_rectangle([x - kw // 2 - 8, k0 - 8, x + kw // 2 + 8, k1 + 8], radius=32, fill=(18, 12, 8))   # a dark edge: apart from the brown
        d.rounded_rectangle([x - kw // 2, k0, x + kw // 2, k1], radius=26, fill=base)
        d.rounded_rectangle([x - kw // 2 + 8, k0 + 8, x + kw // 2 - 8, k0 + 70], radius=18, fill=hi)
        d.rounded_rectangle([x - kw // 2 + 14, k0 + 90, x + kw // 2 - 14, k1 - 20], radius=16, fill=base)
        d.rectangle([x + kw // 2 - 22, k0 + 30, x + kw // 2 - 6, k1 - 14], fill=lo)
        d.rectangle([x - kw // 2 + 6, k1 - 30, x + kw // 2 - 6, k1 - 10], fill=lo)
    return c


def logo():
    img = Image.new("RGB", (W, W), BG)
    b = bars().rotate(28, resample=Image.BICUBIC, expand=True)
    k = 0.46
    b = b.resize((int(b.size[0] * k), int(b.size[1] * k)), Image.LANCZOS)
    img.paste(b, (-60, -270), b)
    d = ImageDraw.Draw(img, "RGBA")
    top = 168 * S
    for y in range(top - 40 * S, W):                     # the bars fade into the dark under the wordmark
        a = min(255, max(0, (y - (top - 40 * S)) * 255 // (40 * S)))
        d.line([0, y, W, y], fill=BG + (a,))
    d.text((W // 2, 196 * S), "B3 ORGAN", font=font(BOLD, 46 * S), fill=(240, 234, 220), anchor="mm")
    d.text((W // 2, 226 * S), "after setBfree  ·  for the FM-1", font=font(MEDIUM, 11 * S), fill=(224, 137, 58), anchor="mm")
    return img.resize((240, 240), Image.LANCZOS)


def main():
    im = logo()
    q = im.quantize(colors=32, method=Image.FASTOCTREE, dither=Image.Dither.NONE, kmeans=4)
    pal = q.getpalette()[:96]
    px = list(q.tobytes())
    out = bytearray()
    i = 0
    while i < len(px):                                   # a byte: colour << 3 | (run - 1), runs of 1 to 8
        c, r = px[i], 1
        while i + r < len(px) and px[i + r] == c and r < 8:
            r += 1
        out.append(c << 3 | (r - 1))
        i += r
    rgb = lambda k: ((pal[3 * k] >> 3) << 11) | ((pal[3 * k + 1] >> 2) << 5) | (pal[3 * k + 2] >> 3)
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* The B3 ORGAN logo, 240 x 240 (tools/gen_logo.py: do not edit): 32 colours (RGB565), then runs: colour << 3 | (run - 1) */",
             "#define LOGO_W 240u", "#define LOGO_H 240u",
             "static const uint16_t LOGO_PAL[32] = {" + ", ".join("0x%04X" % rgb(k) for k in range(32)) + "};",
             "static const uint8_t LOGO_RLE[%d] = {" % len(out)]
    for j in range(0, len(out), 24):
        lines.append("    " + ", ".join(str(v) for v in out[j:j + 24]) + ",")
    lines.append("};")
    (ROOT / "firmware/src/organ_logo.h").write_text("\n".join(lines) + "\n")
    shown = q.convert("RGB")
    shown.save(ROOT / "docs/logo.png")
    shown.resize((480, 480), Image.NEAREST).save(ROOT / "docs/logo@2.png")
    print("organ_logo.h: %d bytes of runs" % len(out))


main()
