"""Renders a D2R lit_mesh model (Granny) with its albedo texture to a PNG.
usage: render.py model alb.texture out.png [yaw_deg] [pitch_deg] [size]"""
import sys, math, struct
import numpy as np
from PIL import Image
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import tools_gr2 as gr2

def texture(path):
    d = open(path, 'rb').read()
    w, h = struct.unpack_from('<II', d, 8)
    size, rel = struct.unpack_from('<II', d, 0x24)
    off = 0x28 + rel
    for n in (3, 7, 1):
        try:
            need = w * h // (2 if n == 1 else 1)
            if need != size: continue
            return np.asarray(Image.frombytes('RGBA', (w, h), d[off:off + size], 'bcn', n).convert('RGBA')).astype(np.float32) / 255
        except Exception:
            pass
    raise SystemExit('texture format')

def meshes(path):
    buf, rt, rn = gr2.load(path)
    g = gr2.G(buf)
    root = g.read(rt, rn)
    out = []
    for m in g.deref(root['Meshes']):
        vd = g.deref(m['PrimaryVertexData'])
        verts = g.deref(vd['Vertices'])
        tp = g.deref(m['PrimaryTopology'])
        idx = [r['Int16'] if 'Int16' in r else list(r.values())[0] for r in g.deref(tp['Indices16'])] or \
              [list(r.values())[0] for r in g.deref(tp['Indices'])]
        P = np.array([v['Position'][:3] for v in verts], np.float32)
        N = np.array([v['Normal'][:3] for v in verts], np.float32) if 'Normal' in verts[0] else None
        key = [k for k in verts[0] if k.startswith('TextureCoordinates')]
        UV = np.array([v[key[0]][:2] for v in verts], np.float32) if key else np.zeros((len(verts), 2), np.float32)
        out.append((m['Name'], P, N, UV, (np.array(idx, np.int64) & 0xFFFF).reshape(-1, 3)))
    return out

def rot(yaw, pitch):
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]], np.float32)
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]], np.float32)
    return Rx @ Ry

def render(parts, tex, yaw=0.0, pitch=0.0, size=256, light=(0.3, 0.5, 0.8)):
    R = rot(math.radians(yaw), math.radians(pitch))
    allp = np.concatenate([p for _, p, _, _, _ in parts])
    c = (allp.max(0) + allp.min(0)) / 2
    img = np.zeros((size, size, 4), np.float32)
    zb = np.full((size, size), -1e30, np.float32)
    pts = [((p - c) @ R.T) for _, p, _, _, _ in parts]
    ext = max(np.abs(np.concatenate(pts)[:, :2]).max(), 1e-6)
    s = (size / 2 - 2) / ext
    L = np.array(light, np.float32); L /= np.linalg.norm(L)
    th, tw = tex.shape[:2]
    for (name, P, N, UV, T), q in zip(parts, pts):
        X = q[:, 0] * s + size / 2
        Y = size / 2 - q[:, 1] * s
        Z = q[:, 2]
        if N is not None:
            n = N @ R.T
            n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)
        for a, b, cc in T:
            xs, ys = X[[a, b, cc]], Y[[a, b, cc]]
            x0, x1 = int(max(0, math.floor(xs.min()))), int(min(size - 1, math.ceil(xs.max())))
            y0, y1 = int(max(0, math.floor(ys.min()))), int(min(size - 1, math.ceil(ys.max())))
            if x0 > x1 or y0 > y1: continue
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
            w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
            w2 = 1 - w0 - w1
            inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
            if not inside.any(): continue
            z = w0 * Z[a] + w1 * Z[b] + w2 * Z[cc]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            m = inside & (z > sub)
            if not m.any(): continue
            u = w0 * UV[a, 0] + w1 * UV[b, 0] + w2 * UV[cc, 0]
            v = w0 * UV[a, 1] + w1 * UV[b, 1] + w2 * UV[cc, 1]
            tx = np.clip((u % 1) * tw, 0, tw - 1).astype(int)
            ty = np.clip((v % 1) * th, 0, th - 1).astype(int)
            col = tex[ty, tx]
            if N is not None:
                nn = (w0[..., None] * n[a] + w1[..., None] * n[b] + w2[..., None] * n[cc])
                nn /= np.maximum(np.linalg.norm(nn, axis=-1, keepdims=True), 1e-6)
                shade = 0.45 + 0.75 * np.clip(nn @ L, 0, 1)
                col = col.copy(); col[..., :3] *= shade[..., None]
            sub[m] = z[m]
            img[y0:y1 + 1, x0:x1 + 1][m] = np.concatenate([np.clip(col[..., :3], 0, 1), np.ones(col.shape[:2] + (1,), np.float32)], -1)[m]
    return Image.fromarray((img * 255).astype(np.uint8), 'RGBA')

if __name__ == '__main__':
    parts = meshes(sys.argv[1])
    for n, P, *_ in parts: print(n, len(P), P.min(0), P.max(0))
    tex = texture(sys.argv[2])
    yaw = float(sys.argv[4]) if len(sys.argv) > 4 else 0
    pitch = float(sys.argv[5]) if len(sys.argv) > 5 else 0
    size = int(sys.argv[6]) if len(sys.argv) > 6 else 256
    render(parts, tex, yaw, pitch, size).save(sys.argv[3])
