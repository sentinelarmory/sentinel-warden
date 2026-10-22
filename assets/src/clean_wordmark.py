"""Removes the leftover background fringe (near-grey speckles) around the
Sentinel Warden wordmark and writes a clean transparent PNG.

Usage: python3 clean_wordmark.py <in.png> <out.png>
"""
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

MIN_SAT = 0.15          # letters are cyan; the fringe is grey/white
MIN_BLOB = 40           # drop isolated coloured specks smaller than this (px)
GROW = 2                # keep highlights/edges within this many px of a letter


def clean(src, dst):
    im = np.asarray(Image.open(src).convert("RGBA")).astype(np.float32)
    rgb, a = im[..., :3], im[..., 3]
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1), 0)

    letters = (sat >= MIN_SAT) & (a > 128)
    labels, n = ndimage.label(letters)
    sizes = ndimage.sum(letters, labels, range(1, n + 1))
    letters = np.isin(labels, np.nonzero(sizes >= MIN_BLOB)[0] + 1)
    letters = ndimage.binary_fill_holes(letters)

    keep = ndimage.binary_dilation(letters, iterations=GROW)
    # soft edge: fade the outermost kept ring
    edge = keep & ~ndimage.binary_erosion(keep, iterations=1)
    alpha = np.where(keep, a, 0)
    alpha = np.where(edge, alpha * 0.6, alpha)

    out = np.dstack([rgb, alpha]).clip(0, 255).astype(np.uint8)
    img = Image.fromarray(out, "RGBA")
    img.crop(img.getbbox()).save(dst)
    print(f"wrote {dst} {img.crop(img.getbbox()).size}, removed {int(((a > 0) & ~keep).sum())} px of fringe")


if __name__ == "__main__":
    clean(sys.argv[1], sys.argv[2])
