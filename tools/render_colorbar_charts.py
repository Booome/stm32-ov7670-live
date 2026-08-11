#!/usr/bin/env python3
"""Render OV7670 colorbar capture into 8-band and 5-band color chart PNGs.

Reads a serial capture log whose data lines look like:
    00000000: 30 40 01 04 30 40 01 04 ...   (32 bytes per line)
Each line holds 32 bytes of the raw frame byte stream, the 8-hex-digit
prefix being the byte offset.  All lines between FRAME_START and
FRAME_END are concatenated into one stream.

Row model (established by prior experiments):
  * The captured byte stream has a 317-byte period.  Pixel-row boundaries are
    the 317-byte steps (stream[i*317:(i+1)*317]), NOT the 320-byte printed row
    boundaries (each print row is 320B = 317B of data + 3 dangling bytes).
  * 317 is odd, so a 2-byte-per-pixel split drops one byte -> 316 bytes =
    158 pixels.  Two alignments are tried:
      - tail mode: drop the LAST byte of the 317-byte row (bytes [0:316])
      - head mode: drop the FIRST byte of the 317-byte row (bytes [1:317])

Encoding: RGB565 big-endian only (OV7670 COM15=0xD0 + ST7735 COLMOD=0x05
confirmed as RGB565, high byte first).  Two outputs total (head + tail).
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


def parse_rows(path, tp=None):
    """Return {byte_offset: 32 bytes} for one complete frame dump.

    Hexdump format: "NNNNNNNN: b0 b1 ... b31" with NNNNNNNN the 8-hex-digit
    byte offset.  Every line between FRAME_START and FRAME_END belongs to the
    same frame; concatenating the dict values in key order rebuilds the
    raw byte stream.  If tp is given (e.g. '10'), only the frame whose
    FRAME_START tag is "[tp=10 ...] FRAME_START" is selected; otherwise the
    first frame dump in the log is used.
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


def draw_bars(colors, outpath, w=800, h=400, labels=True):
    """Draw an 8-bar chart: 8 vertical bars side by side (left-to-right)."""
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


def draw_bar5(colors, outpath, w=800, h=160):
    """Draw the 5-band chart: 5 vertical bars (White Black Red Green Blue)."""
    draw_bars(colors, outpath, w=w, h=h, labels=False)


def render_all(rows, align_mode):
    """Return list of per-row pixel color lists.

    The captured byte stream is split into rows by its 317-byte period first
    (stream[i*317:(i+1)*317]) -- the printed 320-byte boundaries are NOT the
    pixel-row boundaries.  We take the first 120 rows (VF_ROWS).  317 is odd,
    so a 2-byte-per-pixel split needs one byte dropped -> 316 bytes =
    158 pixels.  Two alignments are tried:
      - 'tail': drop the LAST byte of the 317-byte row  -> bytes [0:316]
      - 'head': drop the FIRST byte of the 317-byte row -> bytes [1:317]
    """
    stream = b''.join(rows[r] for r in sorted(rows))
    nrows = min(len(stream) // 317, 120)
    frame = []
    for i in range(nrows):
        win = stream[i * 317:(i + 1) * 317]
        if align_mode == 'head':
            pix = win[1:317]
        else:  # 'tail'
            pix = win[0:316]
        row = []
        for j in range(0, len(pix) - 1, 2):
            row.append(decode_pixel(pix[j], pix[j + 1]))
        frame.append(row)
    return frame


def segment_modal(frame, nseg=8):
    """Modal color of each of the nseg equal horizontal segments across all rows."""
    width = min(len(r) for r in frame)
    segs = [Counter() for _ in range(nseg)]
    for r in frame:
        for x, c in enumerate(r[:width]):
            segs[(x * nseg) // width][c] += 1
    return [s.most_common(1)[0][0] if s else (0, 0, 0) for s in segs]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', help='serial capture log with hexdump lines')
    ap.add_argument('-o', '--outdir', default='colorbar_charts')
    ap.add_argument('--tp', default=None, help='select test pattern frame by its "[tp=NN ...]" tag (default: first frame)')
    args = ap.parse_args()

    rows = parse_rows(args.log, tp=args.tp)
    stream_len = sum(len(b) for b in rows.values())
    nrows = min(stream_len // 317, 120)
    tp_tag = args.tp if args.tp is not None else 'first'
    print(f'parsed {len(rows)} dump lines (tp={tp_tag}), stream={stream_len}B -> {nrows} 317B rows (first 120)')
    os.makedirs(args.outdir, exist_ok=True)
    os.makedirs(os.path.join(args.outdir, 'bands5'), exist_ok=True)

    amodes = ('head', 'tail')
    report = []
    for am in amodes:
        frame = render_all(rows, am)
        if len(frame) < 2:
            print(f'  skip align{am}: empty frame')
            continue
        name = f'rgb565_be_align{am}.png'
        draw_frame(frame, os.path.join(args.outdir, name))
        bar8 = segment_modal(frame)
        bar5 = [bar8[i] for i in EVAL_IDX]
        draw_bar5(bar5, os.path.join(args.outdir, 'bands5', name))
        err = sum(sum((bar5[i][k] - STD_8[idx][k]) ** 2 for k in range(3))
                  for i, idx in enumerate(EVAL_IDX))
        report.append((name, bar8, bar5, err))
    print('done ->', args.outdir)
    for name, bar8, bar5, err in sorted(report, key=lambda t: t[3]):
        b8 = ' '.join(f'{c}' for c in bar8)
        b5 = ' '.join(f'{c}' for c in bar5)
        print(f'{name} err={err}\n   8bar: {b8}\n   5bar: {b5}')


if __name__ == '__main__':
    main()