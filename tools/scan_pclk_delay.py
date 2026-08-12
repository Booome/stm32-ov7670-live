#!/usr/bin/env python3
"""Scan register values by detecting left/right edge artifact width.

Parses a serial capture log with multiple frames tagged by a key=value
pattern in the FRAME_START line.  Detects with --key (e.g. --key dcwctr).
For each frame, counts non-standard edge pixels.  Outputs a comparison table.
"""

import argparse
import re
import os
from collections import Counter

STD_8 = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
         (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
THRESH = 35  # Max Euclidean distance to a standard color to count as "match"


def dec_rgb565(w):
    r = (w >> 11) & 0x1F
    g = (w >> 5) & 0x3F
    b = w & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def is_std(c):
    return min(sum((STD_8[i][k] - c[k]) ** 2 for k in range(3))
               for i in range(8)) <= THRESH ** 2


def parse_frames(path, key, row_bytes=320):
    """Return list of (label, frame_rows) tuples.

    Detects frames by matching --key=value in FRAME_START lines.
    """
    key_pat = r'\[.*' + re.escape(key) + r'=(\S+)'

    frames = []
    hex_lines = []
    current_label = None
    on = False
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            s = line.strip()
            if s.endswith('FRAME_START'):
                m = re.search(key_pat, s)
                if m:
                    current_label = key + '=' + m.group(1)
                    hex_lines = []
                    on = True
                continue
            if s.endswith('FRAME_END'):
                if on and current_label is not None and hex_lines:
                    stream = b''.join(hex_lines)
                    nrows = min(len(stream) // row_bytes, 128)
                    frame = []
                    for ri in range(nrows):
                        win = stream[ri * row_bytes:(ri + 1) * row_bytes]
                        row = []
                        for j in range(0, row_bytes - 1, 2):
                            w = win[j] << 8 | win[j + 1]
                            row.append(dec_rgb565(w))
                        frame.append(row)
                    frames.append((current_label, frame))
                on = False
                continue
            if not on:
                continue
            m = re.match(r'[0-9a-f]{8}: (.*)$', s)
            if not m:
                continue
            fields = m.group(1).split()
            if len(fields) != 32:
                continue
            hex_lines.append(bytes(int(f, 16) for f in fields))
    return frames


def artifact_width(frame):
    """Return (left_px, right_px) = mode of non-std edge pixel count."""
    left_counts = []
    right_counts = []
    for row in frame:
        left = 0
        while left < len(row) and not is_std(row[left]):
            left += 1
        left_counts.append(left)

        right = 0
        while right < len(row) and not is_std(row[-(right + 1)]):
            right += 1
        right_counts.append(right)

    left_mode = Counter(left_counts).most_common(1)[0][0]
    right_mode = Counter(right_counts).most_common(1)[0][0]
    return left_mode, right_mode


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', help='serial capture log with multi-frame hexdumps')
    ap.add_argument('--key', default='dcwctr',
                    help='parameter key to scan (default: dcwctr)')
    ap.add_argument('--row-bytes', type=int, default=320)
    args = ap.parse_args()

    frames = parse_frames(args.log, args.key, row_bytes=args.row_bytes)
    if not frames:
        print(f'No frames found. Check that the log contains [{args.key}=XX] tags.')
        return

    print(f'Found {len(frames)} frames')
    print()
    print(f'{"param":>20s}  {"left_art":>8s}  {"right_art":>9s}')
    print('-' * 48)
    best_left = min(f[1][0] for f in [(l, artifact_width(fr)) for l, fr in frames])
    best_right = min(f[1][1] for f in [(l, artifact_width(fr)) for l, fr in frames])
    for label, frame in frames:
        left, right = artifact_width(frame)
        stars = '  <<< BEST' if left == best_left and right == best_right else ''
        print(f'  {label:>18s}  {left:8d}  {right:9d}{stars}')


if __name__ == '__main__':
    main()
