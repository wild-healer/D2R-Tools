"""Turns a D2R .model (Granny v7, 64-bit, BitKnit sections) a quarter turn
about X so that its -Z end points down: (x, y, z) -> (x, z, -y) for every
Position, Normal and Tangent. Item models are built lying down; the shrine
symbol effect ignores the overlay's orientation, so the mesh itself is turned.

The changed section is written back as a BitKnit stream made of stored
(uncompressed) quanta, so section types and relocations stay as they are;
later file offsets and the header size/CRC are updated.

usage: tools_turn_model.py in.model out.model"""
import struct, sys, zlib
import numpy as np
import tools_gr2 as gr2

def turn(src):
    d = bytearray(src)
    ver, fsize, crc, soff, nsec = struct.unpack_from('<5I', d, 0x20)
    secs = [list(struct.unpack_from('<11I', d, 0x20 + soff + i * 44)) for i in range(nsec)]
    buf, rt, rn = gr2.load_bytes(bytes(d))
    starts = gr2.LAST_STARTS
    g = gr2.G(buf)
    root = g.read(rt, rn)
    plain = {}
    for i, s in enumerate(secs):
        comp, off, csize, usize = s[:4]
        if csize == 0: continue
        raw = d[off:off + csize]
        plain[i] = bytearray(gr2.bitknit(raw, usize) if comp == 4 else raw)
    changed = set()
    for vd in [g.deref(m['PrimaryVertexData']) for m in g.deref(root['Meshes'])]:
        kind, tp, count, at = vd['Vertices']
        members = g.members(tp)
        size = g.size(tp)
        sec = max(i for i in plain if starts[i] <= at)
        base = at - starts[sec]
        offset = 0
        for t, name, _, arr in members:
            width = {17: 2, 13: 1}.get(t)
            if name in ('Position', 'Normal', 'Tangent'):
                assert width and arr >= 3, (name, t, arr)
                fmt = '<%d%s' % (arr, 'h' if width == 2 else 'b')
                for k in range(count):
                    p = base + k * size + offset
                    v = list(struct.unpack_from(fmt, plain[sec], p))
                    v[1], v[2] = v[2], max(-v[1], -(1 << (8 * width - 1)) + 1)  # (x, y, z) -> (x, z, -y)
                    struct.pack_into(fmt, plain[sec], p, *v)
                changed.add(sec)
            offset += g.size_of(t, _, arr)
    out = bytearray(d)
    for sec in sorted(changed, reverse=True):
        data = plain[sec]
        stream = bytearray(struct.pack('<H', 0x75B1))
        for q in range(0, len(data), 0x10000):
            stream += b'\0\0' + data[q:q + 0x10000]
        if len(stream) % 2: stream += b'\0'
        off, csize = secs[sec][1], secs[sec][2]
        delta = len(stream) - csize
        out[off:off + csize] = stream
        for s in secs:
            for f in (1, 7, 9):  # data, relocation and marshalling offsets after the section
                if s[f] >= off + csize:
                    s[f] += delta
        secs[sec][2] = len(stream)
    for i, s in enumerate(secs):
        struct.pack_into('<11I', out, 0x20 + soff + i * 44, *s)
    struct.pack_into('<I', out, 0x24, len(out))
    struct.pack_into('<I', out, 0x28, zlib.crc32(out[0x68:]))
    return bytes(out)

if __name__ == '__main__':
    open(sys.argv[2], 'wb').write(turn(open(sys.argv[1], 'rb').read()))
