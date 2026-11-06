#!/usr/bin/env python3
"""Writes the PDF test files for "Extract Picture from PDF" and what each
picture should look like.

Real producers (img2pdf, ReportLab, qpdf) and hand-built files (pikepdf) for
cases those do not make: soft and stencil masks, color keys, 16 bits, CMYK,
separation colors, LZW, ASCII85, RunLength, form XObjects, object streams,
an incremental update, a broken cross-reference table, encryption and
JPEG 2000.

manifest.json lists, per file, the pictures in drawing order with page,
pixel size, placed resolution and the expected PNG (RGBA) or that it cannot
be decoded; "tolerance" is the largest channel difference allowed (JPEG
decoders differ slightly).

Needs: pip install pikepdf img2pdf reportlab Pillow; qpdf on the PATH.
Run from this folder: python3 make_fixtures.py
Cross-check with poppler: pdfimages -list <file>.pdf
"""
import io
import json
import os
import struct
import subprocess
import zlib

import img2pdf
import pikepdf
from PIL import Image
from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas as rl_canvas

manifest = {}


def pattern_rgb(w, h, seed=0):
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255 // max(1, w - 1) + seed) % 256, (y * 255 // max(1, h - 1)) % 256,
                        (x * y * 7 + seed * 3) % 256)
    return img


def pattern_gray(w, h):
    img = Image.new('L', (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 9 + y * 5) % 256
    return img


def save_expected(name, index, img):
    fn = f'{name}-{index}.png'
    img.convert('RGBA').save(fn)
    return fn


def add(name, pictures):
    manifest[name + '.pdf'] = pictures


# ---------------------------------------------------------------- img2pdf

def img2pdf_case(name, img, fmt, dpi, tolerance=0, expected=None):
    buf = io.BytesIO()
    img.save(buf, format=fmt, dpi=(dpi, dpi), **({'quality': 95} if fmt == 'JPEG' else {}))
    layout = img2pdf.get_fixed_dpi_layout_fun((dpi, dpi))
    with open(name + '.pdf', 'wb') as f:
        f.write(img2pdf.convert(buf.getvalue(), layout_fun=layout))
    if expected is None:
        expected = Image.open(io.BytesIO(buf.getvalue())) if fmt == 'JPEG' else img
    add(name, [{'page': 1, 'width': img.width, 'height': img.height, 'dpi': dpi,
                'png': save_expected(name, 0, expected), 'tolerance': tolerance}])


img2pdf_case('img2pdf-rgb', pattern_rgb(40, 30), 'PNG', 150)
img2pdf_case('img2pdf-gray', pattern_gray(40, 30), 'PNG', 300)
img2pdf_case('img2pdf-jpeg', pattern_rgb(48, 32, 5), 'JPEG', 72, tolerance=6)
pal = pattern_rgb(40, 30, 9).quantize(16)
img2pdf_case('img2pdf-indexed', pal, 'PNG', 200)
bilevel = Image.new('1', (37, 21))
for y in range(21):
    for x in range(37):
        bilevel.putpixel((x, y), 1 if (x // 3 + y // 2) % 2 else 0)
img2pdf_case('img2pdf-bilevel', bilevel, 'PNG', 300)
cmyk = pattern_rgb(40, 30, 2).convert('CMYK')
img2pdf_case('img2pdf-cmyk-jpeg', cmyk, 'JPEG', 96, tolerance=12)

# ---------------------------------------------------------------- ReportLab

def reportlab_case():
    name = 'reportlab-pages'
    logo = pattern_rgb(8, 8, 1)
    photo = Image.new('RGBA', (60, 40))
    base = pattern_rgb(60, 40, 4)
    for y in range(40):
        for x in range(60):
            r, g, b = base.getpixel((x, y))
            photo.putpixel((x, y), (r, g, b, 255 if x < 40 else 0))   # right third transparent
    second = pattern_rgb(30, 50, 7)
    c = rl_canvas.Canvas(name + '.pdf', pagesize=(400, 400))
    c.drawImage(ImageReader(logo), 20, 360, width=16, height=16)            # 8 px on 16 pt: 36 dpi
    c.drawImage(ImageReader(photo), 50, 50, width=14.4, height=9.6, mask='auto')   # 300 dpi
    c.showPage()
    c.drawImage(ImageReader(second), 10, 10, width=72, height=120)          # 30 dpi
    c.showPage()
    c.save()
    add(name, [
        {'page': 1, 'width': 8, 'height': 8, 'dpi': 36, 'png': save_expected(name, 0, logo), 'tolerance': 0},
        {'page': 1, 'width': 60, 'height': 40, 'dpi': 300, 'png': save_expected(name, 1, photo), 'tolerance': 0},
        {'page': 2, 'width': 30, 'height': 50, 'dpi': 30, 'png': save_expected(name, 2, second), 'tolerance': 0},
    ])


reportlab_case()

# ---------------------------------------------------------------- by hand

def lzw_encode(data):
    """PDF LZW, EarlyChange 1."""
    table = {bytes([i]): i for i in range(256)}
    next_code, width = 258, 9
    out_bits, nbits, out = 0, 0, bytearray()

    def emit(code):
        nonlocal out_bits, nbits
        out_bits = (out_bits << width) | code
        nbits += width
        while nbits >= 8:
            out.append((out_bits >> (nbits - 8)) & 0xff)
            nbits -= 8

    emit(256)
    w = b''
    for byte in data:
        wc = w + bytes([byte])
        if wc in table:
            w = wc
            continue
        emit(table[w])
        table[wc] = next_code
        next_code += 1
        if next_code + 1 > (1 << width) and width < 12:
            width += 1
        if next_code >= 4094:
            emit(256)
            table = {bytes([i]): i for i in range(256)}
            next_code, width = 258, 9
        w = bytes([byte])
    if w:
        emit(table[w])
    emit(257)
    if nbits:
        out.append((out_bits << (8 - nbits)) & 0xff)
    return bytes(out)


def ascii85(data):
    import base64
    return base64.a85encode(data) + b'~>'


def runlength(data):
    out = bytearray()
    for i in range(0, len(data), 128):
        chunk = data[i:i + 128]
        out.append(len(chunk) - 1)
        out += chunk
    return bytes(out + b'\x80')


def page_with(pdf, xobjects, content, size=(200, 200)):
    page = pikepdf.Dictionary(Type=pikepdf.Name.Page, MediaBox=[0, 0, size[0], size[1]],
                              Resources=pikepdf.Dictionary(XObject=pikepdf.Dictionary(xobjects)),
                              Contents=pdf.make_stream(content))
    pdf.pages.append(pikepdf.Page(page))


def image_stream(pdf, data, w, h, bpc, cs, filters=None, **extra):
    s = pdf.make_stream(data)
    s.Type = pikepdf.Name.XObject
    s.Subtype = pikepdf.Name.Image
    s.Width, s.Height, s.BitsPerComponent = w, h, bpc
    if cs is not None:
        s.ColorSpace = cs
    if filters:
        s.Filter = filters
    for k, v in extra.items():
        s[pikepdf.Name('/' + k)] = v
    return s


def place(w_pt, h_pt, x=10, y=10):
    return f'q {w_pt} 0 0 {h_pt} {x} {y} cm /Im0 Do Q'.encode()


def raw_rgb(img):
    return img.convert('RGB').tobytes()


def manual(name, build, pictures):
    pdf = pikepdf.new()
    build(pdf)
    pdf.save(name + '.pdf', compress_streams=False, object_stream_mode=pikepdf.ObjectStreamMode.disable)
    add(name, pictures)


# Soft mask: RGB + gray transparency.
rgb = pattern_rgb(30, 20, 3)
alpha = Image.new('L', (30, 20))
for y in range(20):
    for x in range(30):
        alpha.putpixel((x, y), x * 255 // 29)
with_alpha = rgb.copy()
with_alpha.putalpha(alpha)


def build_smask(pdf):
    sm = image_stream(pdf, zlib.compress(alpha.tobytes()), 30, 20, 8, pikepdf.Name.DeviceGray, pikepdf.Name.FlateDecode)
    im = image_stream(pdf, zlib.compress(raw_rgb(rgb)), 30, 20, 8, pikepdf.Name.DeviceRGB, pikepdf.Name.FlateDecode, SMask=sm)
    page_with(pdf, {'/Im0': im}, place(7.2, 4.8))   # 300 dpi


manual('smask', build_smask, [{'page': 1, 'width': 30, 'height': 20, 'dpi': 300,
                               'png': save_expected('smask', 0, with_alpha), 'tolerance': 0}])

# CMYK, uncompressed samples; expected with the plain formula.
cmyk_vals = []
cmyk_expected = Image.new('RGB', (20, 10))
for y in range(10):
    for x in range(20):
        c, m, yy, k = (x * 13) % 256, (y * 25) % 256, (x * y * 3) % 256, (x * 5) % 128
        cmyk_vals += [c, m, yy, k]
        f = lambda v: round(255 * (1 - v / 255) * (1 - k / 255))
        cmyk_expected.putpixel((x, y), (f(c), f(m), f(yy)))


def build_cmyk(pdf):
    im = image_stream(pdf, bytes(cmyk_vals), 20, 10, 8, pikepdf.Name.DeviceCMYK)
    page_with(pdf, {'/Im0': im}, place(14.4, 7.2))   # 100 dpi


manual('cmyk-raw', build_cmyk, [{'page': 1, 'width': 20, 'height': 10, 'dpi': 100,
                                 'png': save_expected('cmyk-raw', 0, cmyk_expected), 'tolerance': 1}])

# 16 bits per sample (the high byte counts).
rgb16 = pattern_rgb(12, 9, 6)
data16 = bytearray()
for v in raw_rgb(rgb16):
    data16 += struct.pack('>H', v * 256 + 3)


def build_16(pdf):
    im = image_stream(pdf, zlib.compress(bytes(data16)), 12, 9, 16, pikepdf.Name.DeviceRGB, pikepdf.Name.FlateDecode)
    page_with(pdf, {'/Im0': im}, place(8.64, 6.48))   # 100 dpi


manual('rgb16', build_16, [{'page': 1, 'width': 12, 'height': 9, 'dpi': 100,
                            'png': save_expected('rgb16', 0, rgb16), 'tolerance': 0}])

# LZW inside ASCII85, and RunLength inside ASCIIHex.
lzw_img = pattern_rgb(33, 17, 8)
rl_img = pattern_gray(25, 15)


def build_filters(pdf):
    a = image_stream(pdf, ascii85(lzw_encode(raw_rgb(lzw_img))), 33, 17, 8, pikepdf.Name.DeviceRGB,
                     pikepdf.Array([pikepdf.Name.ASCII85Decode, pikepdf.Name.LZWDecode]))
    b = image_stream(pdf, runlength(rl_img.tobytes()).hex().encode() + b'>', 25, 15, 8, pikepdf.Name.DeviceGray,
                     pikepdf.Array([pikepdf.Name.ASCIIHexDecode, pikepdf.Name.RunLengthDecode]))
    page_with(pdf, {'/Im0': a, '/Im1': b},
              b'q 23.76 0 0 12.24 10 10 cm /Im0 Do Q q 18 0 0 10.8 50 50 cm /Im1 Do Q')


manual('lzw-a85-rl-hex', build_filters, [
    {'page': 1, 'width': 33, 'height': 17, 'dpi': 100, 'png': save_expected('lzw-a85-rl-hex', 0, lzw_img), 'tolerance': 0},
    {'page': 1, 'width': 25, 'height': 15, 'dpi': 100, 'png': save_expected('lzw-a85-rl-hex', 1, rl_img), 'tolerance': 0},
])

# Stencil mask (/Mask stream) and color-key mask (/Mask array).
base = pattern_rgb(16, 16, 11)
stencil = Image.new('1', (16, 16))
masked = base.convert('RGBA')
for y in range(16):
    for x in range(16):
        hidden = (x + y) % 4 == 0   # sample 1: not painted
        stencil.putpixel((x, y), 1 if hidden else 0)
        if hidden:
            masked.putpixel((x, y), (0, 0, 0, 0))
key = pattern_rgb(16, 8, 0)
keyed = key.convert('RGBA')
for y in range(8):
    for x in range(16):
        r, g, b = key.getpixel((x, y))
        if r <= 60 and g <= 255 and b <= 255:
            keyed.putpixel((x, y), (0, 0, 0, 0))


def build_masks(pdf):
    m = pdf.make_stream(stencil.tobytes())
    m.Type, m.Subtype = pikepdf.Name.XObject, pikepdf.Name.Image
    m.Width, m.Height, m.ImageMask = 16, 16, True
    a = image_stream(pdf, raw_rgb(base), 16, 16, 8, pikepdf.Name.DeviceRGB, Mask=m)
    b = image_stream(pdf, raw_rgb(key), 16, 8, 8, pikepdf.Name.DeviceRGB, Mask=pikepdf.Array([0, 60, 0, 255, 0, 255]))
    page_with(pdf, {'/Im0': a, '/Im1': b},
              b'q 11.52 0 0 11.52 10 10 cm /Im0 Do Q q 11.52 0 0 5.76 50 50 cm /Im1 Do Q')


manual('masks', build_masks, [
    {'page': 1, 'width': 16, 'height': 16, 'dpi': 100, 'png': save_expected('masks', 0, masked), 'tolerance': 0},
    {'page': 1, 'width': 16, 'height': 8, 'dpi': 100, 'png': save_expected('masks', 1, keyed), 'tolerance': 0},
])

# A picture inside a form XObject with its own matrix; separation color.
sep_img = pattern_gray(20, 20)
sep_expected = Image.eval(sep_img, lambda v: 255 - v)


def build_form(pdf):
    im = image_stream(pdf, sep_img.tobytes(), 20, 20, 8,
                      pikepdf.Array([pikepdf.Name.Separation, pikepdf.Name('/Spot'), pikepdf.Name.DeviceCMYK,
                                     pikepdf.Dictionary(FunctionType=2, Domain=[0, 1], C0=[0, 0, 0, 0],
                                                        C1=[0, 0, 0, 1], N=1)]))
    form = pdf.make_stream(b'q 1 0 0 1 0 0 cm /Pic Do Q')
    form.Type, form.Subtype = pikepdf.Name.XObject, pikepdf.Name.Form
    form.BBox = [0, 0, 100, 100]
    form.Matrix = [0.5, 0, 0, 0.5, 0, 0]
    form.Resources = pikepdf.Dictionary(XObject=pikepdf.Dictionary({'/Pic': im}))
    page_with(pdf, {'/Fm0': form}, b'q 28.8 0 0 28.8 20 20 cm /Fm0 Do Q')   # 14.4 pt: 100 dpi


manual('form-separation', build_form, [{'page': 1, 'width': 20, 'height': 20, 'dpi': 100,
                                        'png': save_expected('form-separation', 0, sep_expected), 'tolerance': 1}])

# JPEG 2000 (not decoded) next to a normal picture.
def build_jpx(pdf):
    j = image_stream(pdf, b'\x00\x00\x00\x0cjP  \r\n\x87\n' + b'\x00' * 64, 50, 40, 8, pikepdf.Name.DeviceRGB,
                     pikepdf.Name.JPXDecode)
    ok = image_stream(pdf, raw_rgb(pattern_rgb(10, 10)), 10, 10, 8, pikepdf.Name.DeviceRGB)
    page_with(pdf, {'/Im0': j, '/Im1': ok}, b'q 36 0 0 28.8 0 0 cm /Im0 Do Q q 7.2 0 0 7.2 80 80 cm /Im1 Do Q')


manual('jpx', build_jpx, [
    {'page': 1, 'width': 50, 'height': 40, 'dpi': 100, 'problem': True},
    {'page': 1, 'width': 10, 'height': 10, 'dpi': 100, 'png': save_expected('jpx', 1, pattern_rgb(10, 10)), 'tolerance': 0},
])

# ---------------------------------------------------------------- file structure

# Object streams (the page, its resources ... compressed together).
subprocess.run(['qpdf', '--object-streams=generate', 'reportlab-pages.pdf', 'objstm.pdf'], check=True)
add('objstm', manifest['reportlab-pages.pdf'])

# An incremental update that replaces the picture: the new one counts.
newer = pattern_rgb(30, 20, 99)
with open('smask.pdf', 'rb') as f:
    original = f.read()
pdf = pikepdf.open('smask.pdf')
im = pdf.pages[0].Resources.XObject['/Im0']
num = im.objgen[0]
pdf.close()
body = zlib.compress(raw_rgb(newer))
update = (f'\n{num} 0 obj\n<< /Type /XObject /Subtype /Image /Width 30 /Height 20 /BitsPerComponent 8 '
          f'/ColorSpace /DeviceRGB /Filter /FlateDecode /Length {len(body)} >>\nstream\n').encode() + body + b'\nendstream\nendobj\n'
start = len(original) + 1
prev = int(original[original.rindex(b'startxref') + 9:].split()[0])
size = int(pikepdf.open('smask.pdf').trailer.Size)
root = pikepdf.open('smask.pdf').trailer.Root.objgen[0]
xref_at = len(original) + len(update)
tail = (f'xref\n0 1\n0000000000 65535 f \n{num} 1\n{start:010d} 00000 n \n'
        f'trailer\n<< /Size {size} /Root {root} 0 R /Prev {prev} >>\nstartxref\n{xref_at}\n%%EOF\n').encode()
with open('incremental.pdf', 'wb') as f:
    f.write(original + update + tail)
add('incremental', [{'page': 1, 'width': 30, 'height': 20, 'dpi': 300,
                     'png': save_expected('incremental', 0, newer), 'tolerance': 0}])

# A cross-reference table pointing nowhere.
with open('img2pdf-rgb.pdf', 'rb') as f:
    data = bytearray(f.read())
at = data.rindex(b'startxref') + 10
data[at:at + 3] = b'999'
with open('broken-xref.pdf', 'wb') as f:
    f.write(data)
add('broken-xref', manifest['img2pdf-rgb.pdf'])

# Encrypted (empty user password): refused.
subprocess.run(['qpdf', '--encrypt', '', 'owner', '256', '--', 'img2pdf-rgb.pdf', 'encrypted.pdf'], check=True)
add('encrypted', {'error': True})

with open('manifest.json', 'w') as f:
    json.dump(manifest, f, indent=1, sort_keys=True)
print('wrote', len(manifest), 'PDF files')
