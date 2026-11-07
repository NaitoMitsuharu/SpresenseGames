"""Convert a glTF model (e.g. a CC0 Poly Haven asset) into a compact particle file for the Atelier.

The app never renders the model: it only samples these points, so real scanned/artist geometry
can be shown as particles. Output layout (little endian), see LightBoxModels.cpp:
  "LBPT" | uint32 version=1 | uint32 count | float3 boundsMin | float3 boundsMax (UE cm, Z up)
  count x 14 bytes { int16 x,y,z (bounds-normalised to +-32767) | uint8 r,g,b (sRGB) | uint8 pad | int8 nx,ny,nz | uint8 pad }
Points are shuffled, so any prefix is a uniform subsample.

Usage: python -I Tools/PointCloud/ModelToPoints.py <model.gltf> <out.lbpt> [--count 262144] [--cutout 0.06]
"""
import argparse, json, math, struct
from pathlib import Path
import numpy as np
from PIL import Image

COMPONENT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def accessor(gltf, buffers, index):
    a = gltf['accessors'][index]; view = gltf['bufferViews'][a['bufferView']]
    dtype = np.dtype(COMPONENT[a['componentType']]); width = WIDTH[a['type']]
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0); stride = view.get('byteStride', 0)
    raw = buffers[view['buffer']]
    if stride and stride != dtype.itemsize * width:
        rows = np.frombuffer(raw, np.uint8, a['count'] * stride, start).reshape(a['count'], stride)
        return rows[:, :dtype.itemsize * width].copy().view(dtype).reshape(a['count'], width)
    return np.frombuffer(raw, dtype, a['count'] * width, start).reshape(a['count'], width) if width > 1 else np.frombuffer(raw, dtype, a['count'], start)


def node_matrix(node):
    if 'matrix' in node: return np.array(node['matrix'], float).reshape(4, 4).T
    t = np.array(node.get('translation', [0, 0, 0]), float); s = np.array(node.get('scale', [1, 1, 1]), float)
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    r = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    m = np.eye(4); m[:3, :3] = r * s; m[:3, 3] = t; return m


def main():
    p = argparse.ArgumentParser(); p.add_argument('gltf', type=Path); p.add_argument('out', type=Path)
    p.add_argument('--count', type=int, default=262144); p.add_argument('--cutout', type=float, default=.06, help='texels darker than this (0..1) are treated as transparent on alpha-blended/masked materials')
    p.add_argument('--seed', type=int, default=7); a = p.parse_args()
    gltf = json.loads(a.gltf.read_text(encoding='utf-8')); base = a.gltf.parent
    buffers = [(base / b['uri']).read_bytes() for b in gltf['buffers']]
    images = {}
    def texture(material):
        info = material.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        if info is None: return None, None
        source = gltf['textures'][info['index']]['source']
        if source not in images:
            img = Image.open(base / gltf['images'][source]['uri']); images[source] = np.asarray(img.convert('RGBA'), np.uint8)
        xf = info.get('extensions', {}).get('KHR_texture_transform', {})
        return images[source], (np.array(xf.get('offset', [0, 0]), float), np.array(xf.get('scale', [1, 1]), float))
    rng = np.random.default_rng(a.seed)
    tris = []  # per primitive: (positions[n,3,3], normals[n,3,3], uvs[n,3,2], tex, xf, cutout)
    for n in gltf['scenes'][gltf.get('scene', 0)]['nodes']:
        stack = [(n, np.eye(4))]
        while stack:
            i, parent = stack.pop(); node = gltf['nodes'][i]; m = parent @ node_matrix(node)
            for c in node.get('children', []): stack.append((c, m))
            if 'mesh' not in node: continue
            for prim in gltf['meshes'][node['mesh']]['primitives']:
                pos = accessor(gltf, buffers, prim['attributes']['POSITION']).astype(float)
                nor = accessor(gltf, buffers, prim['attributes']['NORMAL']).astype(float) if 'NORMAL' in prim['attributes'] else np.zeros_like(pos)
                uv = accessor(gltf, buffers, prim['attributes']['TEXCOORD_0']).astype(float) if 'TEXCOORD_0' in prim['attributes'] else np.zeros((len(pos), 2))
                idx = accessor(gltf, buffers, prim['indices']).astype(np.int64).reshape(-1, 3) if 'indices' in prim else np.arange(len(pos)).reshape(-1, 3)
                pos = pos @ m[:3, :3].T + m[:3, 3]; nor = nor @ np.linalg.inv(m[:3, :3])
                mat = gltf['materials'][prim['material']] if 'material' in prim else {}
                tex, xf = texture(mat)
                cut = mat.get('alphaMode', 'OPAQUE') in ('BLEND', 'MASK')
                tris.append((pos[idx], nor[idx], uv[idx], tex, xf, cut, mat.get('pbrMetallicRoughness', {}).get('baseColorFactor', [1, 1, 1, 1])))
    areas = np.concatenate([np.linalg.norm(np.cross(t[0][:, 1] - t[0][:, 0], t[0][:, 2] - t[0][:, 0]), axis=1) * .5 for t in tris])
    owner = np.concatenate([np.full(len(t[0]), k) for k, t in enumerate(tris)]); local = np.concatenate([np.arange(len(t[0])) for t in tris])
    cdf = np.cumsum(areas); cdf /= cdf[-1]
    out_pos, out_col, out_nor = [], [], []; have = 0
    while have < a.count:
        batch = (a.count - have) * 2
        pick = np.minimum(np.searchsorted(cdf, rng.random(batch)), len(cdf) - 1)
        r1, r2 = np.sqrt(rng.random(batch)), rng.random(batch); w = np.stack([1 - r1, r1 * (1 - r2), r1 * r2], 1)
        for k, t in enumerate(tris):
            sel = owner[pick] == k
            if not sel.any(): continue
            li = local[pick[sel]]; ww = w[sel][:, :, None]
            P = (t[0][li] * ww).sum(1); N = (t[1][li] * ww).sum(1); UV = (t[2][li] * ww).sum(1)
            if t[3] is not None:
                if t[4] is not None: UV = UV * t[4][1] + t[4][0]
                h, wd = t[3].shape[:2]; x = (np.mod(UV[:, 0], 1) * (wd - 1)).astype(int); y = (np.mod(UV[:, 1], 1) * (h - 1)).astype(int)
                C = t[3][y, x].astype(float) / 255
            else:
                C = np.ones((len(P), 4))
            C[:, :3] *= np.array(t[6][:3])
            keep = np.ones(len(P), bool)
            if t[5]: keep = (C[:, 3] > .5) & (C[:, :3].max(1) > a.cutout)
            out_pos.append(P[keep]); out_col.append(C[keep, :3]); out_nor.append(N[keep]); have += int(keep.sum())
    P = np.concatenate(out_pos)[:a.count]; C = np.concatenate(out_col)[:a.count]; N = np.concatenate(out_nor)[:a.count]
    order = rng.permutation(len(P)); P, C, N = P[order], C[order], N[order]
    # glTF is Y-up metres (right handed); UE is Z-up centimetres (left handed): (x, y, z) -> (x, -z, y) * 100.
    P = np.stack([P[:, 0], -P[:, 2], P[:, 1]], 1) * 100; N = np.stack([N[:, 0], -N[:, 2], N[:, 1]], 1)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    lo, hi = P.min(0), P.max(0); q = np.round((P - (lo + hi) / 2) / np.maximum((hi - lo) / 2, 1e-6) * 32767).astype(np.int16)
    rec = np.zeros(len(P), dtype=[('p', '<i2', 3), ('c', 'u1', 3), ('pad', 'u1'), ('n', 'i1', 3), ('pad2', 'u1')])
    rec['p'] = q; rec['c'] = np.round(np.clip(C, 0, 1) * 255).astype(np.uint8); rec['n'] = np.round(N * 127).astype(np.int8)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(b'LBPT' + struct.pack('<II6f', 1, len(P), *lo, *hi) + rec.tobytes())
    print(f'{a.out.name}: {len(P)} points, size {np.round(hi - lo).tolist()} cm, {a.out.stat().st_size / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
