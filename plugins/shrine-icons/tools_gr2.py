"""Minimal Granny2 reader for D2R's HD .model files (Granny v7, 64-bit,
sections compressed with BitKnit): decompresses the sections, applies the
relocations and walks the file's self-describing type tree into dicts/lists.
The BitKnit decoder follows the clean-room implementation in the maclarian
crate (src/formats/gr2/decompress.rs), itself after Knit and pybg3."""
import struct, sys

RANS = 0x10000

class Table:
    def __init__(s, fb, vocab, lb):
        s.fb, s.vocab, s.shift = fb, vocab, fb - lb
        s.sums = [0] * (vocab + 1)
        s.lookup = [0] * (1 << lb)
    def find(s, code):
        sym = s.lookup[code >> s.shift]
        while code >= s.sums[sym + 1]:
            sym += 1
        return sym
    def finish(s):
        code, sym, nxt = 0, 0, s.sums[1]
        mx, step = 1 << s.fb, 1 << s.shift
        while code < mx:
            if code < nxt:
                s.lookup[code >> s.shift] = sym
                code += step
            else:
                sym += 1
                nxt = s.sums[sym + 1]

class Model:
    def __init__(s, interval, vocab, nmin, fb, lb):
        neq = vocab - nmin
        total = 1 << fb
        s.interval = interval
        s.incr = (total - vocab) // interval
        s.last = 1 + total - vocab - s.incr * interval
        s.cdf = Table(fb, vocab, lb)
        for i in range(neq):
            s.cdf.sums[i] = ((total - nmin) * i // neq) & 0xFFFF
        for i in range(neq, vocab + 1):
            s.cdf.sums[i] = (total - vocab + i) & 0xFFFF
        s.acc = [1] * vocab
        s.cdf.finish()
        s.counter = 0
    def observe(s, sym):
        s.acc[sym] = (s.acc[sym] + s.incr) & 0xFFFF
        s.counter = (s.counter + 1) % s.interval
        if s.counter == 0:
            s.acc[sym] = (s.acc[sym] + s.last) & 0xFFFF
            tot = 0
            sums = s.cdf.sums
            for i in range(1, s.cdf.vocab + 1):
                tot = (tot + s.acc[i - 1]) & 0xFFFFFFFF
                old = sums[i]
                sums[i] = (old + (((tot - old) & 0xFFFFFFFF) // 2)) & 0xFFFF
                s.acc[i - 1] = 1
            s.cdf.finish()

class Stream:
    def __init__(s, data): s.d, s.p = data, 0
    def pop(s):
        d, p = s.d, s.p
        if p + 1 < len(d):
            s.p += 2; return d[p] | d[p + 1] << 8
        if p < len(d):
            s.p += 1; return d[p]
        return 0
    def peek(s):
        d, p = s.d, s.p
        if p + 1 < len(d): return d[p] | d[p + 1] << 8
        if p < len(d): return d[p]
        return 0

def bitknit(data, size):
    out = bytearray(size)
    st = Stream(data)
    assert st.pop() == 0x75B1, 'bitknit magic'
    cmd = [Model(1024, 300, 36, 15, 10) for _ in range(4)]
    cache = [Model(1024, 40, 0, 15, 10) for _ in range(4)]
    offm = Model(1024, 21, 0, 15, 10)
    entries, order = [1] * 8, 0x76543210
    delta = 1
    idx = 0
    s = [RANS, RANS]  # s[0] = state1, s[1] = state2

    def refill(v):
        return ((v << 16) | st.pop()) if v < RANS else v
    def popbits(n):
        v = s[0]
        r = v & ((1 << n) - 1)
        s[0] = refill(v >> n)
        s[0], s[1] = s[1], s[0]
        return r
    def popcdf(m):
        v = s[0]
        c = m.cdf
        code = v & ((1 << c.fb) - 1)
        sym = c.find(code)
        freq = c.sums[sym + 1] - c.sums[sym]
        s[0] = refill((v >> c.fb) * freq + code - c.sums[sym])
        m.observe(sym)
        s[0], s[1] = s[1], s[0]
        return sym

    while idx < size:
        boundary = min((idx & 0xFFFF0000) + 0x10000, size)
        if st.peek() == 0:
            st.pop()
            n = min(len(st.d) - st.p, boundary - idx)
            out[idx:idx + n] = st.d[st.p:st.p + n]
            idx += n; st.p += n
            continue
        i0 = st.pop(); i1 = st.pop()
        merged = (i0 << 16) | i1
        split = merged & 0xF
        merged = refill(merged >> 4)
        s1 = refill(merged >> split)
        s2 = (merged << 16) | st.pop()
        s2 &= (1 << (16 + split)) - 1
        s2 |= 1 << (16 + split)
        s[0], s[1] = s1, s2
        if idx == 0:
            out[0] = popbits(8); idx = 1
        while idx < boundary:
            mi = idx % 4
            c = popcdf(cmd[mi])
            if c >= 256:
                if c < 288:
                    length = c - 254
                else:
                    ll = c - 287
                    length = (1 << ll) + popbits(ll) + 32
                ref = popcdf(cache[mi])
                if ref < 8:
                    slot = (order >> (ref * 4)) & 0xF
                    mask = ((16 << (ref * 4)) - 1) & 0xFFFFFFFF
                    rot = ((order << 4) | slot) & mask
                    order = (order & ~mask & 0xFFFFFFFF) | rot
                    off = entries[slot]
                else:
                    ol = popcdf(offm)
                    ob = popbits(ol % 16)
                    if ol >= 16:
                        ob = (ob << 16) | st.pop()
                    off = ((32 << ol) + (ob << 5) - 32 + (ref - 7)) & 0xFFFFFFFF
                    i7 = (order >> 28) & 0xF; i6 = (order >> 24) & 0xF
                    entries[i7] = entries[i6]; entries[i6] = off
                delta = off
                assert off <= idx, ('bad offset', off, idx)
                for _ in range(length):
                    if idx >= size: break
                    out[idx] = out[idx - off]; idx += 1
            else:
                db = out[idx - delta] if idx >= delta else 0
                out[idx] = (c + db) & 0xFF; idx += 1
    return bytes(out)

# ---- container ----

def load(path):
    return load_bytes(open(path, 'rb').read())

LAST_STARTS = []

def load_bytes(d):
    ver, fsize, crc, soff, nsec = struct.unpack_from('<5I', d, 0x20)
    rtype = struct.unpack_from('<2I', d, 0x34)
    rnode = struct.unpack_from('<2I', d, 0x3C)
    secs = []
    base = 0x20 + soff
    for i in range(nsec):
        secs.append(struct.unpack_from('<11I', d, base + i * 44))
    datas = []
    for comp, off, csize, usize, align, f16, f8, roff, nrel, moff, nmix in secs:
        if csize == 0:
            datas.append(bytearray(usize)); continue
        raw = d[off:off + csize]
        if comp == 0: datas.append(bytearray(raw))
        elif comp == 4: datas.append(bytearray(bitknit(raw, usize)))
        else: raise SystemExit('compression %d' % comp)
    # one flat buffer
    starts, buf = [], bytearray()
    for x in datas:
        while len(buf) % 16: buf.append(0)
        starts.append(len(buf)); buf += x
    buf += b'\0' * 16
    for si, (comp, off, csize, usize, align, f16, f8, roff, nrel, moff, nmix) in enumerate(secs):
        if not nrel: continue
        if comp == 4:
            clen = struct.unpack_from('<I', d, roff)[0]
            rel = bitknit(d[roff + 4:roff + 4 + clen], nrel * 12)
        else:
            rel = d[roff:roff + nrel * 12]
        for k in range(nrel):
            o, ts, to = struct.unpack_from('<3I', rel, k * 12)
            struct.pack_into('<Q', buf, starts[si] + o, starts[ts] + to + 1)  # +1: 0 stays null
    LAST_STARTS[:] = starts
    return buf, starts[rtype[0]] + rtype[1], starts[rnode[0]] + rnode[1]

SIZES = {1: None, 2: 8, 3: 12, 4: 12, 5: 16, 7: 20, 8: 8, 9: 68, 10: 4, 11: 1, 12: 1, 13: 1, 14: 1,
         15: 2, 16: 2, 17: 2, 18: 2, 19: 4, 20: 4, 21: 2, 22: 8}
FMT = {10: 'f', 11: 'b', 12: 'B', 13: 'b', 14: 'B', 15: 'h', 16: 'H', 17: 'h', 18: 'H', 19: 'i', 20: 'I', 21: 'e'}

class G:
    def __init__(s, buf): s.b = buf
    def ptr(s, at):
        v = struct.unpack_from('<Q', s.b, at)[0]
        return v - 1 if v else None
    def cstr(s, at):
        if at is None: return None
        e = s.b.index(0, at); return s.b[at:e].decode('latin1')
    def members(s, tp):
        out = []
        while tp is not None:
            t = struct.unpack_from('<I', s.b, tp)[0]
            if t == 0: break
            name = s.cstr(s.ptr(tp + 4))
            dfn = s.ptr(tp + 12)
            arr = struct.unpack_from('<I', s.b, tp + 20)[0]
            out.append((t, name, dfn, arr))
            tp += 44
        return out
    def size_of(s, t, dfn, arr):
        return (s.size(dfn) if t == 1 else SIZES[t]) * max(arr, 1)
    def size(s, tp):
        tot = 0
        for t, n, dfn, arr in s.members(tp):
            one = s.size(dfn) if t == 1 else SIZES[t]
            tot += one * max(arr, 1)
        return tot
    def read(s, tp, at, depth=0, flat=False):
        obj = {}
        for t, name, dfn, arr in s.members(tp):
            cnt = max(arr, 1)
            vals = []
            for _ in range(cnt):
                if t == 1:
                    vals.append(s.read(dfn, at, depth + 1)); at += s.size(dfn)
                elif t in FMT:
                    vals.append(struct.unpack_from('<' + FMT[t], s.b, at)[0]); at += SIZES[t]
                elif t == 8:
                    vals.append(s.cstr(s.ptr(at))); at += 8
                elif t == 9:
                    vals.append(struct.unpack_from('<I16f', s.b, at)); at += 68
                elif t == 2:
                    p = s.ptr(at); vals.append(('ref', dfn, p)); at += 8
                elif t == 3:
                    c = struct.unpack_from('<I', s.b, at)[0]; p = s.ptr(at + 4)
                    vals.append(('arr', dfn, c, p)); at += 12
                elif t == 4:
                    c = struct.unpack_from('<I', s.b, at)[0]; p = s.ptr(at + 4)
                    vals.append(('refarr', dfn, c, p)); at += 12
                elif t == 5:
                    vals.append(('var', s.ptr(at), s.ptr(at + 8))); at += 16
                elif t == 7:
                    tpp = s.ptr(at); c = struct.unpack_from('<I', s.b, at + 8)[0]; p = s.ptr(at + 12)
                    vals.append(('vararr', tpp, c, p)); at += 20
                elif t == 22:
                    vals.append(None); at += 8
            obj[name] = vals if arr else vals[0]
        return obj
    # helpers to deref
    def deref(s, v):
        if v is None: return None
        k = v[0]
        if k == 'ref': return None if v[2] is None else s.read(v[1], v[2])
        if k == 'arr':
            sz = s.size(v[1]); return [s.read(v[1], v[3] + i * sz) for i in range(v[2])] if v[3] is not None else []
        if k == 'refarr':
            return [s.read(v[1], s.ptr(v[3] + i * 8)) for i in range(v[2])] if v[3] is not None else []
        if k == 'var': return None if v[2] is None else s.read(v[1], v[2])
        if k == 'vararr':
            sz = s.size(v[1]); return [s.read(v[1], v[3] + i * sz) for i in range(v[2])] if v[3] is not None else []

if __name__ == '__main__':
    buf, rt, rn = load(sys.argv[1])
    g = G(buf)
    root = g.read(rt, rn)
    for k, v in root.items():
        print(k, v if not isinstance(v, tuple) else v[:1] + v[2:])
