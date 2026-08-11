#!/usr/bin/env python3
"""Render OV7670 colorbar capture into 8-band and 5-band color chart PNGs.

Reads a serial capture log whose data lines look like:
    000: 30 40 01 04 30 40 01 04 ...   (32 bytes per line)
Each "NNN:" line holds 32 bytes of the raw frame byte stream, NNN being
the 8-hex-digit byte offset (line 00000000 covers stream bytes [0:32]).  All
lines between FRAME_START and FRAME_END are concatenated into one stream.

Row model (established by prior experiments):
  * The captured byte stream has a 317-byte period.  Pixel-row boundaries are
    the 317-byte steps (stream[i*317:(i+1)*317]), NOT the 320-byte printed row
    boundaries (each print row is 320B = 317B of data + 3 dangling bytes).
  * 317 is odd, so a 2-byte-per-pixel split drops one byte -> 316 bytes =
    158 pixels.  Two alignments are tried:
      - tail mode: drop the LAST byte of the 317-byte row (bytes [0:316])
      - head mode: drop the FIRST byte of the 317-byte row (bytes [1:317])
  * Each mode is rendered under every plausible color encoding.

The 5-band chart takes the measured colors at the White/Black/Red/Green/Blue
positions (indices 0,7,5,3,6) -- positions, not the standard values.
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
    """Return {line_offset: 32 bytes} for one complete frame dump.

    New hexdump format: "NNN: b0 b1 ... b31" with NNN the 3-hex-digit line
    offset.  Every line between FRAME_START and FRAME_END belongs to the
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


def dec_rgb555(w):
    r = (w >> 10) & 0x1F
    g = (w >> 5) & 0x1F
    b = w & 0x1F
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def yuv2rgb(y, u, v):
    c = y - 16
    r = 1.164 * c + 1.596 * (v - 128)
    g = 1.164 * c - 0.813 * (v - 128) - 0.391 * (u - 128)
    b = 1.164 * c + 2.018 * (u - 128)
    return (max(0, min(255, int(round(r)))),
            max(0, min(255, int(round(g)))),
            max(0, min(255, int(round(b)))))


def decode_pixel(enc, b0, b1):
    """Decode one 2-byte pixel under the given encoding."""
    if enc == 'rgb565_le':
        return dec_rgb565(b1 << 8 | b0)
    if enc == 'rgb565_be':
        return dec_rgb565(b0 << 8 | b1)
    if enc == 'rgb555_le':
        return dec_rgb555(b1 << 8 | b0)
    if enc == 'rgb555_be':
        return dec_rgb555(b0 << 8 | b1)
    if enc == 'grb_g_r':
        return (b1, b0, b0)
    if enc == 'grb_g_b':
        return (b0, b0, b1)
    if enc == 'grb_r_g':
        return (b0, b1, b1)
    if enc == 'grb_b_g':
        return (b1, b1, b0)
    if enc == 'yuv_yuyv':
        return yuv2rgb(b0, b1, b1)  # approximated, UV unknown here
    if enc == 'yuv_uyvy':
        return yuv2rgb(b1, b0, b0)
    return (0, 0, 0)


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


def render_all(rows, enc, align_mode):
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
            row.append(decode_pixel(enc, pix[j], pix[j + 1]))
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
    ap.add_argument('log', help='serial capture log with "NNN: b0 b1 ... b31" hexdump lines')
    ap.add_argument('-o', '--outdir', default='colorbar_charts')
    ap.add_argument('--tp', default=None, help='select test pattern frame by its "[tp=NN ...]" tag (default: first frame)')
    ap.add_argument('--align', choices=('both', 'head', 'tail'), default='both',
                    help='pixel alignment: drop first byte (head) or last byte (tail)')
    args = ap.parse_args()

    rows = parse_rows(args.log, tp=args.tp)
    stream_len = sum(len(b) for b in rows.values())
    nrows = min(stream_len // 317, 120)
    tp_tag = args.tp if args.tp is not None else 'first'
    print(f'parsed {len(rows)} dump lines (tp={tp_tag}), stream={stream_len}B -> {nrows} 317B rows (first 120)')
    os.makedirs(args.outdir, exist_ok=True)
    os.makedirs(os.path.join(args.outdir, 'bands5'), exist_ok=True)

    encodings = ['rgb565_le', 'rgb565_be', 'rgb555_le', 'rgb555_be',
                 'grb_g_r', 'grb_g_b', 'grb_r_g', 'grb_b_g',
                 'yuv_yuyv', 'yuv_uyvy']
    amodes = ('head', 'tail') if args.align == 'both' else (args.align,)
    report = []
    for enc in encodings:
        for am in amodes:
            frame = render_all(rows, enc, am)
            if len(frame) < 2:
                print(f'  skip {enc} {am}: empty frame')
                continue
            name = f'{enc}_align{am}.png'
            # 8-band chart: render the raw serial data directly (vertical bars)
            draw_frame(frame, os.path.join(args.outdir, name))
            # segment modal colors, then pick the eval positions
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