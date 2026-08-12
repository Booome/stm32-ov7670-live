#!/usr/bin/env python3
"""Render OV7670 colorbar capture into color chart PNGs.

Reads a serial capture log whose data lines look like:
    00000000: 30 40 01 04 30 40 01 04 ...   (32 bytes per line)
Each line holds 32 bytes of the raw frame byte stream, the 8-hex-digit
prefix being the byte offset.  All lines between FRAME_START and
FRAME_END are concatenated into one stream.

Outputs:
  - rgb565_be_aligntail.png       raw frame (natural size)
  - rgb565_be_aligntail_circles.png  frame with sampling squares overlaid
  - bands8.png                    8-color bar chart (800x200)
  - bands5.png                    5-color bar chart (500x200)

Sampling uses multi-row mean averaging over configurable square regions
centered at known colorband positions (--centers, --half).
"""

import argparse
import os
import re
from collections import Counter

from PIL import Image

FORE = (255, 0, 0)
BACK = (0, 0, 0)

STD_8 = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
         (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
EVAL_IDX = (0, 7, 5, 3, 6)  # White Black Red Green Blue positions


def parse_rows(path, tp=None, tag=None):
    """Return {byte_offset: 32 bytes} for one complete frame dump.

    Hexdump format: "NNNNNNNN: b0 b1 ... b31" with NNNNNNNN the 8-hex-digit
    byte offset.  Every line between FRAME_START and FRAME_END belongs to the
    same frame; concatenating the dict values in key order rebuilds the
    raw byte stream.  If tp is given (e.g. '10'), only the frame whose
    FRAME_START tag is "[tp=10 ...]" is selected; if tag is given, the
    FRAME_START line must also contain the substring.  Use --tag to
    disambiguate frames that share the same tp value.  If neither is given,
    the first frame dump in the log is used.
    """
    rows = {}
    on = False
    want = None
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            s = line.strip()
            if s.endswith('FRAME_START'):
                m = re.match(r'\[tp=(\d+)\s', s)
                frm_tp = m.group(1) if m else None
                if tp is not None and frm_tp != tp:
                    on = False
                    continue
                if tag is not None and tag not in s:
                    on = False
                    continue
                rows = {}
                want = frm_tp
                on = True
                continue
            if s.endswith('FRAME_END'):
                if on and want is not None:
                    break
                continue
            if not on:
                continue
            m = re.match(r'([0-9a-f]{8}): (.*)$', s)
            if not m:
                continue
            off = int(m.group(1), 16)
            fields = m.group(2).split()
            if len(fields) != 32:
                continue
            data = bytes(int(f, 16) for f in fields)
            rows[off] = data
    if not rows:
        raise ValueError('no frame dump between FRAME_START and FRAME_END')
    return rows


def dec_rgb565(w):
    r = (w >> 11) & 0x1F
    g = (w >> 5) & 0x3F
    b = w & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def decode_pixel(b0, b1):
    """Decode one 2-byte RGB565 pixel (big-endian: b0=high, b1=low)."""
    return dec_rgb565(b0 << 8 | b1)


def nearest_std(c):
    """Return index of the nearest standard 8-bar color."""
    return min(range(8), key=lambda i: sum((STD_8[i][k] - c[k]) ** 2 for k in range(3)))


def modal_color(cells):
    if not cells:
        return (0, 0, 0)
    return Counter(cells).most_common(1)[0][0]


def draw_frame(frame, outpath, scale=4):
    """Render the raw pixel frame directly (vertical bars: x = pixel, y = row)."""
    width = min(len(r) for r in frame)
    h = len(frame)
    img = Image.new('RGB', (width, h))
    pix = img.load()
    for y, row in enumerate(frame):
        for x in range(width):
            pix[x, y] = row[x]
    img = img.resize((width * scale, h * scale), Image.NEAREST)
    img.save(outpath)


def draw_squares(img, centers, half=3, scale=4):
    """Overlay sampling square regions on the frame image.

    Each square is (2*half+1) pixels wide, centered at the given pixel
    coordinate, spanning the full image height.  A label is drawn above.
    """
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img)
    names = ['White', 'Yellow', 'Cyan', 'Green',
             'Magenta', 'Red', 'Blue', 'Black']
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
              (255, 0, 255), (0, 255, 255), (128, 128, 128), (255, 255, 255)]
    for i, (cx, name, color) in enumerate(zip(centers, names, colors)):
        x0 = (cx - half) * scale
        x1 = (cx + half + 1) * scale
        y0 = img.height // 2 - half * scale
        y1 = img.height // 2 + (half + 1) * scale
        draw.rectangle([x0, y0, x1, y1], outline=color, width=2)
        draw.text((x0 + 2, y0 - 12), name, fill=color)
    return img


def sample_bars(stream, nrows, centers, half=3, row_bytes=317):
    """Multi-row mean sampling over square regions.

    For each center pixel, averages all pixels in [cx-half, cx+half] across
    nrows rows.  When row_bytes is odd, one byte is dropped (tail) to keep
    2-byte pixel alignment; when even, all bytes are used.  Returns a list
    of 8 (R, G, B) tuples.
    """
    row_w = row_bytes
    odd = row_bytes % 2 == 1
    usable = row_bytes - 1 if odd else row_bytes
    npix = usable // 2
    bar8 = []
    for cx in centers:
        sum_r, sum_g, sum_b, count = 0, 0, 0, 0
        for ri in range(nrows):
            win = stream[ri * row_w : ri * row_w + usable]
            for dx in range(-half, half + 1):
                px = cx + dx
                if 0 <= px < npix:
                    w = win[px * 2] << 8 | win[px * 2 + 1]
                    r = (w >> 11) & 0x1F
                    g = (w >> 5) & 0x3F
                    b = w & 0x1F
                    sum_r += (r << 3) | (r >> 2)
                    sum_g += (g << 2) | (g >> 4)
                    sum_b += (b << 3) | (b >> 2)
                    count += 1
        bar8.append((round(sum_r / count), round(sum_g / count),
                     round(sum_b / count)))
    return bar8


def draw_bars(colors, outpath, w=800, h=200, labels=True):
    """Draw a color bar chart: vertical bars side by side (left-to-right)."""
    img = Image.new('RGB', (w, h))
    draw = __import__('PIL.ImageDraw', fromlist=['ImageDraw']).Draw(img)
    seg = w // len(colors)
    for i, c in enumerate(colors):
        x0 = i * seg
        x1 = (i + 1) * seg if i < len(colors) - 1 else w
        draw.rectangle([x0, 0, x1, h], fill=c)
        if labels:
            draw.text((x0 + 4, 4), f'{i}', fill=(0, 0, 0))
    img.save(outpath)


def draw_bar5(colors, outpath, w=500, h=200):
    """Draw the 5-band chart: 5 vertical bars (White Black Red Green Blue)."""
    draw_bars(colors, outpath, w=w, h=h, labels=False)


def render_all(rows, align_mode, row_bytes=317):
    """Return list of per-row pixel color lists.

    The captured byte stream is split into rows by its row_bytes period
    (stream[i*row_bytes:(i+1)*row_bytes]).  When row_bytes is odd, a
    2-byte-per-pixel split needs one byte dropped, and two alignments are
    tried:
      - 'tail': drop the LAST byte of the row  -> bytes [0:row_bytes-1]
      - 'head': drop the FIRST byte of the row -> bytes [1:row_bytes]
    When row_bytes is even, no byte is dropped (row_bytes/2 pixels).
    """
    stream = b''.join(rows[r] for r in sorted(rows))
    nrows = min(len(stream) // row_bytes, 128)
    odd = row_bytes % 2 == 1
    frame = []
    for i in range(nrows):
        win = stream[i * row_bytes:(i + 1) * row_bytes]
        if odd:
            if align_mode == 'head':
                pix = win[1:row_bytes]
            else:  # 'tail'
                pix = win[0:row_bytes - 1]
        else:
            pix = win
        row = []
        for j in range(0, len(pix) - 1, 2):
            row.append(decode_pixel(pix[j], pix[j + 1]))
        frame.append(row)
    return frame


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', help='serial capture log with hexdump lines')
    ap.add_argument('-o', '--outdir', default='colorbar_charts')
    ap.add_argument('--tp', default=None,
                    help='select test pattern frame by its "[tp=NN ...]" tag '
                         '(default: first frame)')
    ap.add_argument('--tag', default=None,
                    help='further filter frame by substring in FRAME_START line '
                         '(used with --tp to disambiguate frames sharing the same tp)')
    ap.add_argument('--centers', default='14,34,54,74,96,116,136,153',
                    help='sampling center positions, comma-separated pixel '
                         'coords (default: 14,34,54,74,96,116,136,153)')
    ap.add_argument('--half', type=int, default=3,
                    help='sampling half-side length in pixels (default: 3)')
    ap.add_argument('--row-bytes', type=int, default=317,
                    help='byte period of one row (default: 317 for QVGA)')
    args = ap.parse_args()

    centers = [int(x) for x in args.centers.split(',')]
    row_bytes = args.row_bytes

    rows = parse_rows(args.log, tp=args.tp, tag=args.tag)
    stream = b''.join(rows[r] for r in sorted(rows))
    nrows = min(len(stream) // row_bytes, 128)
    tp_tag = args.tp if args.tp is not None else 'first'
    print(f'parsed {len(rows)} dump lines (tp={tp_tag}), '
          f'stream={len(stream)}B -> {nrows} {row_bytes}B rows')
    os.makedirs(args.outdir, exist_ok=True)

    # 1. Raw frame image (natural size)
    frame = render_all(rows, 'tail', row_bytes=row_bytes)
    frame_path = os.path.join(args.outdir, 'rgb565_be_aligntail.png')
    draw_frame(frame, frame_path)

    # 2. Frame image with sampling squares overlaid
    frame_img = Image.open(frame_path)
    draw_squares(frame_img, centers, half=args.half)
    frame_img.save(os.path.join(args.outdir,
                                'rgb565_be_aligntail_circles.png'))

    # 3. Multi-row mean sampling
    bar8 = sample_bars(stream, nrows, centers, half=args.half,
                       row_bytes=row_bytes)
    bar5 = [bar8[i] for i in EVAL_IDX]

    # 4. bands8 (800x200)
    draw_bars(bar8, os.path.join(args.outdir, 'bands8.png'), w=800, h=200)

    # 5. bands5 (500x200)
    draw_bar5(bar5, os.path.join(args.outdir, 'bands5.png'), w=500, h=200)

    # 6. Report
    err = sum(sum((bar5[i][k] - STD_8[idx][k]) ** 2 for k in range(3))
              for i, idx in enumerate(EVAL_IDX))
    print(f'bands8: {" ".join(str(c) for c in bar8)}')
    print(f'bands5: {" ".join(str(c) for c in bar5)}')
    print(f'err={err}')


if __name__ == '__main__':
    main()