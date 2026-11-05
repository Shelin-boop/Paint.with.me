#!/usr/bin/env python3
"""Writes the synthetic PSD/PSB test files in this folder (standard library only).

They cover what GIMP and Krita do not easily produce: CMYK, grayscale, ZIP
compression with prediction, 16-bit layers stored in an "Lr16" block, the
large-document format .psb, and a file with no layers (combined picture only).
Run from this folder: python3 make_fixtures.py
Check them with an independent reader, e.g. psd-tools:
  python3 -c "from psd_tools import PSDImage; print(PSDImage.open('cmyk.psd'))"
"""
import struct
import zlib

def be16(v): return struct.pack('>H', v)
def bei16(v): return struct.pack('>h', v)
def be32(v): return struct.pack('>I', v)
def bei32(v): return struct.pack('>i', v)
def be64(v): return struct.pack('>Q', v)

def packbits_row(row):
    out = bytearray()
    for i in range(0, len(row), 128):
        chunk = row[i:i + 128]
        out.append(len(chunk) - 1)
        out += chunk
    return bytes(out)

def channel_block(plane, w, h, comp, depth, psb):
    """plane: list of h rows, each a list of w samples (0-255 or 0-65535)."""
    bpc = depth // 8
    rows = []
    for r in plane:
        if bpc == 1:
            rows.append(bytes(r))
        else:
            rows.append(b''.join(be16(v) for v in r))
    if w == 0 or h == 0:
        return be16(0)
    if comp == 0:
        return be16(0) + b''.join(rows)
    if comp == 1:
        enc = [packbits_row(r) for r in rows]
        counts = b''.join((be32 if psb else be16)(len(e)) for e in enc)
        return be16(1) + counts + b''.join(enc)
    if comp in (2, 3):
        data = bytearray()
        for r in rows:
            r = bytearray(r)
            if comp == 3:
                if bpc == 1:
                    for x in range(len(r) - 1, 0, -1):
                        r[x] = (r[x] - r[x - 1]) & 0xff
                else:
                    vals = [struct.unpack('>H', bytes(r[2 * i:2 * i + 2]))[0] for i in range(w)]
                    deltas = [vals[0]] + [(vals[i] - vals[i - 1]) & 0xffff for i in range(1, w)]
                    r = bytearray(b''.join(be16(v) for v in deltas))
            data += r
        return be16(comp) + zlib.compress(bytes(data))
    raise ValueError(comp)

def pascal(name, mult):
    b = name.encode('latin-1')
    s = bytes([len(b)]) + b
    while len(s) % mult:
        s += b'\0'
    return s

def tagged(key, data, psb=False):
    while len(data) % 4:
        data += b'\0'
    return b'8BIM' + key + be32(len(data)) + data

def luni(name):
    return tagged(b'luni', be32(len(name)) + b''.join(be16(ord(c)) for c in name))

def layer_info(layers, depth, psb, comp, merged_alpha=True):
    """layers: dicts with name, rect (top, left, bottom, right), channels {id: plane},
    mode key, opacity, hidden."""
    records = b''
    data = b''
    for L in layers:
        top, left, bottom, right = L['rect']
        w, h = right - left, bottom - top
        blocks = []
        for cid in sorted(L['channels'], key=lambda c: (c != -1, c)):
            blk = channel_block(L['channels'][cid], w, h, L.get('comp', comp), depth, psb)
            blocks.append((cid, blk))
            data += blk
        rec = bei32(top) + bei32(left) + bei32(bottom) + bei32(right) + be16(len(blocks))
        for cid, blk in blocks:
            rec += bei16(cid) + (be64 if psb else be32)(len(blk))
        flags = 0x08 | (0x02 if L.get('hidden') else 0)
        rec += b'8BIM' + L.get('mode', b'norm') + bytes([L.get('opacity', 255), 0, flags, 0])
        extra = be32(0) + be32(0) + pascal(L['name'], 4) + luni(L['name'])
        rec += be32(len(extra)) + extra
        records += rec
    count = -len(layers) if merged_alpha else len(layers)
    body = bei16(count) + records + data
    while len(body) % 4:
        body += b'\0'
    return body

def header(w, h, channels, depth, mode, psb=False):
    return (b'8BPS' + be16(2 if psb else 1) + b'\0' * 6 + be16(channels) + be32(h) + be32(w)
            + be16(depth) + be16(mode) + be32(0))

def resolution(dpi):
    fixed = int(round(dpi * 65536))
    data = be32(fixed) + be16(1) + be16(1) + be32(fixed) + be16(1) + be16(1)
    block = b'8BIM' + be16(0x03ED) + be16(0) + be32(len(data)) + data
    return be32(len(block)) + block

def merged_raw(planes, depth):
    out = be16(0)
    for plane in planes:
        for r in plane:
            out += bytes(r) if depth == 8 else b''.join(be16(v) for v in r)
    return out

def flat_planes(w, h, value):
    return [[value] * w for _ in range(h)]

def layered_file(path, w, h, mode, depth, layers, psb=False, comp=1, colors=3, lr16=False, dpi=None):
    li = layer_info(layers, depth, psb, comp)
    if lr16:
        # 16-bit: the layers live in an "Lr16" block; the regular list is empty.
        lms = (be64 if psb else be32)(0) + be32(0) + b'8BIM' + b'Lr16' + (be64 if psb else be32)(len(li)) + li
    else:
        lms = (be64 if psb else be32)(len(li)) + li + be32(0)
    out = header(w, h, colors + 1, depth, mode, psb)
    out += resolution(dpi) if dpi else be32(0)
    out += (be64 if psb else be32)(len(lms)) + lms
    maxv = 255 if depth == 8 else 65535
    out += merged_raw([flat_planes(w, h, maxv)] * (colors + 1), depth)
    open(path, 'wb').write(out)

def main():
    W, H = 6, 4
    # CMYK: stored inverted (255 = no ink). Left half cyan ink, right half black ink.
    c = [[0 if x < 3 else 255 for x in range(W)] for _ in range(H)]
    m = flat_planes(W, H, 255)
    y = flat_planes(W, H, 255)
    k = [[255 if x < 3 else 0 for x in range(W)] for _ in range(H)]
    a = flat_planes(W, H, 255)
    layered_file('cmyk.psd', W, H, 4, 8, [
        {'name': 'Ink', 'rect': (0, 0, H, W), 'channels': {0: c, 1: m, 2: y, 3: k, -1: a}}],
        colors=4)

    # Grayscale with a hidden second layer at 40 % opacity.
    g = [[x * 50 for x in range(W)] for _ in range(H)]
    layered_file('gray.psd', W, H, 1, 8, [
        {'name': 'Ramp', 'rect': (0, 0, H, W), 'channels': {0: g, -1: a}},
        {'name': 'Hidden', 'rect': (1, 1, 3, 3), 'channels': {0: flat_planes(2, 2, 0), -1: flat_planes(2, 2, 255)},
         'hidden': True, 'opacity': 102}], colors=1)

    # 16-bit RGB in an Lr16 block, ZIP with prediction, alpha ramp, 300 dpi.
    r16 = [[65535] * W for _ in range(H)]
    g16 = [[x * 13107 for x in range(W)] for _ in range(H)]
    b16 = flat_planes(W, H, 0)
    a16 = [[(y_ + 1) * 16383 for _ in range(W)] for y_ in range(H)]
    layered_file('zip16-lr16.psd', W, H, 3, 16, [
        {'name': 'Deep', 'rect': (0, 0, H, W), 'channels': {0: r16, 1: g16, 2: b16, -1: a16}, 'mode': b'mul '}],
        comp=3, lr16=True, dpi=300)

    # .psb: a layer reaching 2 px past the left edge; 4-byte RLE counts.
    red = flat_planes(4, 2, 255)
    zero = flat_planes(4, 2, 0)
    layered_file('large.psb', W, H, 3, 8, [
        {'name': 'Background', 'rect': (0, 0, H, W), 'channels': {0: flat_planes(W, H, 255), 1: flat_planes(W, H, 255), 2: flat_planes(W, H, 255), -1: a}},
        {'name': 'Past the edge', 'rect': (1, -2, 3, 2), 'channels': {0: red, 1: zero, 2: zero, -1: flat_planes(4, 2, 255)}, 'mode': b'sLit'}],
        psb=True)

    # No layers at all: just the combined picture (raw, 3 channels).
    out = header(W, H, 3, 8, 3) + be32(0) + be32(0)
    out += merged_raw([[[200] * W for _ in range(H)], [[100] * W for _ in range(H)], [[50] * W for _ in range(H)]], 8)
    open('flat.psd', 'wb').write(out)

if __name__ == '__main__':
    main()
