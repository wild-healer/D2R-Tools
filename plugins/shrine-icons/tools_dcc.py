"""Minimal DCC decoder (after Paul Siramy's format notes): first direction,
all frames, as palette indices (0 = transparent). Used by
tools_make_mod_files.py for the classic booster shrine symbols."""
import struct, sys
from PIL import Image

class Bits:
    def __init__(self, data, start_bit=0, end_bit=None):
        self.d = data; self.p = start_bit
    def read(self, n):
        v = 0
        for i in range(n):
            byte = self.d[(self.p) >> 3]
            v |= ((byte >> (self.p & 7)) & 1) << i
            self.p += 1
        return v
    def signed(self, n):
        v = self.read(n)
        if n and v & (1 << (n - 1)): v -= 1 << n
        return v

WIDTH = [0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32]

def decode(path):
    b = open(path, 'rb').read()
    sig, ver, ndirs = b[0], b[1], b[2]
    fpd, = struct.unpack_from('<I', b, 3)
    dir_off = struct.unpack_from('<%dI' % ndirs, b, 15)
    start = dir_off[0]
    end = dir_off[1] if ndirs > 1 else len(b)
    data = b[start:end] + b'\0' * 8
    bs = Bits(data)
    bs.read(32)
    comp = bs.read(2)
    v0, wb, hb, xb, yb, ob, cb = (WIDTH[bs.read(4)] for _ in range(7))
    frames = []
    for _ in range(fpd):
        f = dict(v0=bs.read(v0), w=bs.read(wb), h=bs.read(hb), x=bs.signed(xb), y=bs.signed(yb), opt=bs.read(ob), coded=bs.read(cb), bottom=bs.read(1))
        f['xmin'] = f['x']; f['xmax'] = f['x'] + f['w']
        f['ymin'] = f['y'] - f['h'] + 1; f['ymax'] = f['y'] + 1
        frames.append(f)
    if any(f['opt'] for f in frames):
        bs.p = (bs.p + 7) & ~7
        bs.p += 8 * sum(f['opt'] for f in frames)
    eq_size = bs.read(20) if comp & 2 else 0
    mask_size = bs.read(20)
    enc_size = raw_size = 0
    if comp & 1:
        enc_size = bs.read(20); raw_size = bs.read(20)
    values = [i for i in range(256) if bs.read(1)]
    pos = bs.p
    eq = Bits(data, pos); pos += eq_size
    mask = Bits(data, pos); pos += mask_size
    enc = Bits(data, pos); pos += enc_size
    raw = Bits(data, pos); pos += raw_size
    code = Bits(data, pos)
    dxmin = min(f['xmin'] for f in frames); dymin = min(f['ymin'] for f in frames)
    dxmax = max(f['xmax'] for f in frames); dymax = max(f['ymax'] for f in frames)
    dw, dh = dxmax - dxmin, dymax - dymin
    ncw = 1 + (dw - 1) // 4; nch = 1 + (dh - 1) // 4
    def split(size, first):
        if size - first <= 1: return [size]
        t = size - first - 1
        n = 2 + t // 4
        if t % 4 == 0: n -= 1
        cells = [first] + [4] * (n - 2)
        cells.append(size - sum(cells))
        return cells
    cellbuf = [None] * (ncw * nch)
    plan = []   # per frame: list of (dir cell idx, x, y, w, h, entry or None)
    for f in frames:
        cw = split(f['w'], 4 - ((f['xmin'] - dxmin) % 4))
        ch = split(f['h'], 4 - ((f['ymin'] - dymin) % 4))
        c0x = (f['xmin'] - dxmin) // 4; c0y = (f['ymin'] - dymin) // 4
        cells = []
        yy = f['ymin'] - dymin
        for j, hgt in enumerate(ch):
            xx = f['xmin'] - dxmin
            for i, wid in enumerate(cw):
                cur = (c0y + j) * ncw + c0x + i
                nxt = False
                if cellbuf[cur] is not None:
                    t = eq.read(1) if eq_size else 0
                    if t == 0: pm = mask.read(4)
                    else: nxt = True
                else:
                    pm = 0x0F
                entry = None
                if not nxt:
                    rp = [0] * 4; last = 0
                    nb = bin(pm).count('1')
                    et = enc.read(1) if (nb and enc_size) else 0
                    dec = 0
                    for k in range(nb):
                        if et:
                            rp[k] = raw.read(8)
                        else:
                            rp[k] = last
                            d = code.read(4); rp[k] += d
                            while d == 15:
                                d = code.read(4); rp[k] += d
                        if rp[k] == last:
                            rp[k] = 0; break
                        last = rp[k]; dec += 1
                    old = cellbuf[cur]
                    val = [0] * 4; ci = dec - 1
                    for k in range(4):
                        if pm & (1 << k):
                            if ci >= 0: val[k] = rp[ci]; ci -= 1
                        else:
                            val[k] = old[k]
                    entry = val
                    cellbuf[cur] = val
                cells.append((xx, yy, wid, hgt, entry))
                xx += wid
            yy += hgt
        plan.append(cells)
    bmp = [[0] * dw for _ in range(dh)]
    images = []
    for f, cells in zip(frames, plan):
        for xx, yy, wid, hgt, entry in cells:
            if entry is None: continue
            v = [values[e] if e < len(values) else 0 for e in entry]
            if entry[0] == entry[1]:
                for y in range(hgt):
                    for x in range(wid): bmp[yy + y][xx + x] = v[0]
            else:
                nbb = 1 if entry[1] == entry[2] else 2
                for y in range(hgt):
                    for x in range(wid): bmp[yy + y][xx + x] = v[code.read(nbb)]
        img = [row[f['xmin'] - dxmin:f['xmax'] - dxmin] for row in bmp[f['ymin'] - dymin:f['ymax'] - dymin]]
        images.append(img)
    return frames, images

def load_palette(path):
    raw = open(path, 'rb').read()
    return [(raw[i * 3 + 2], raw[i * 3 + 1], raw[i * 3]) for i in range(256)]

def to_rgba(img, pal):
    h = len(img); w = len(img[0]) if h else 0
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            i = img[y][x]
            if i: px[x, y] = pal[i] + (255,)
    return im
