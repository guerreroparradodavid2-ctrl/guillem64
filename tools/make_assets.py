#!/usr/bin/env python3
"""Genera src/assets.h y src/level.h (con colisiones extra) a partir de tools/data2.json.
Mallas reducidas por agrupacion de vertices, texturas 32x32 (pera 64x32) en RGBA5551."""
import base64, io, json, math, os, re, struct, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.join(HERE, "..", "src")
D = json.load(open(os.path.join(HERE, "data2.json")))
STATIC_SKIP = ("body2", "head", "hair", "arms", "XGOK", "GOK", "Cylinder", "unamed", "Plane_165", "squidward", "Capsule", "mesh_id", "Collectable")
LIGHT = np.array([.4, .8, .3]); LIGHT /= np.linalg.norm(LIGHT)

def mesh(fn):
    m = D["meshes"][fn]; n = m["n"]
    P = np.frombuffer(base64.b64decode(m["pos"]), "<f4").reshape(n, 3).astype(float)
    N = np.frombuffer(base64.b64decode(m["nrm"]), "i1").reshape(n, 3).astype(float) / 127.
    U = np.frombuffer(base64.b64decode(m["uv"]), "<f4").reshape(n, 2).astype(float)
    subs = []
    for s in m["subs"]:
        I = np.frombuffer(base64.b64decode(s["i"]), "<u4" if m["wide"] else "<u2").reshape(-1, 3).astype(int)
        subs.append(I)
    return P, N, U, subs

def decimate(P, N, U, I, target):
    """Agrupacion de vertices por celdas (posicion + uv). Devuelve P,N,U,I nuevos."""
    if len(I) <= target: 
        used = np.unique(I); remap = -np.ones(len(P), int); remap[used] = np.arange(len(used))
        return P[used], N[used], U[used], remap[I]
    used = np.unique(I); Pu, Nu, Uu = P[used], N[used], U[used]; remap = -np.ones(len(P), int); remap[used] = np.arange(len(used)); Iu = remap[I]
    lo = np.linalg.norm(Pu.max(0) - Pu.min(0)) / 600; hi = np.linalg.norm(Pu.max(0) - Pu.min(0)) / 2
    best = None
    for _ in range(18):
        cell = math.sqrt(lo * hi)
        key = np.concatenate([np.floor(Pu / cell), np.floor(Uu * 3)], 1).astype(np.int64)
        _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.reshape(-1)
        T = inv[Iu]; ok = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2]); T = T[ok]
        if len(T) > target: lo = cell
        else: hi = cell; best = (inv, T)
    if best is None: best = (inv, T)
    inv, T = best; k = inv.max() + 1; cnt = np.bincount(inv, minlength=k).astype(float)
    def avg(A): return np.stack([np.bincount(inv, A[:, j], k) / cnt for j in range(A.shape[1])], 1)
    P2, N2, U2 = avg(Pu), avg(Nu), avg(Uu)
    u2 = np.unique(T); r2 = -np.ones(k, int); r2[u2] = np.arange(len(u2))
    return P2[u2], N2[u2], U2[u2], r2[T]

def shade(N): 
    d = np.clip(N @ LIGHT, 0, 1); return .55 + .45 * d

textures = {}; tex_order = []
def tex_id(name):
    if name is None: return -1
    if name not in textures:
        data = base64.b64decode(D["tex"][name].split(",")[1]); im = Image.open(io.BytesIO(data)).convert("RGBA")
        size = (64, 32) if name == "Binary_1_75.png" else (32, 32)
        im = im.resize(size, Image.LANCZOS).transpose(Image.FLIP_TOP_BOTTOM)
        a = np.array(im).astype(int)
        v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 3) << 6) | ((a[..., 2] >> 3) << 1) | 1
        textures[name] = (len(tex_order), size, v.reshape(-1)); tex_order.append(name)
    return textures[name][0]

parts = []; models = []; model_key = {}
def add_model(key, builder):
    if key in model_key: return model_key[key]
    first = len(parts); n = 0
    for p in builder():
        parts.append(p); n += 1
    models.append((first, n)); model_key[key] = len(models) - 1; return len(models) - 1

def make_part(P, N, U, I, tex, col, xform=None):
    if xform is not None: P, N = xform(P, N)
    U = U.copy()
    if len(U) and tex >= 0: U -= np.floor(U.min(0))
    s = shade(N)[:, None]; base = np.array(col if tex < 0 else [1, 1, 1])[None]
    C = np.clip(np.concatenate([base * s, np.ones((len(P), 1))], 1) * 255, 0, 255).astype(int)
    return dict(P=P, U=U, C=C, I=I.reshape(-1), tex=tex)

def build_model(fn, tl, cl, target, xform=None):
    def b():
        P, N, U, subs = mesh(fn); tot = sum(len(s) for s in subs); out = []
        for k, I in enumerate(subs):
            if len(I) == 0: continue
            t = max(6, int(target * len(I) / tot))
            P2, N2, U2, I2 = decimate(P, N, U, I, t)
            tn = tl[min(k, len(tl) - 1)]; c = cl[min(k, len(cl) - 1)]
            out.append(make_part(P2, N2, U2, I2, tex_id(tn), c, xform))
        return out
    return b

# ---- instancias estaticas
cnt = {}
for i in D["inst"]:
    cnt[i[0]] = cnt.get(i[0], 0) + 1
insts = []
for fn, tl, cl, M, near, go in D["inst"]:
    if fn.startswith(STATIC_SKIP): continue
    tris = sum(len(s) for s in mesh(fn)[3])
    tgt = 24 if fn.startswith("Sphere") else (min(100, tris) if cnt[fn] > 3 else min(300, tris))
    key = (fn, tuple(tl), tuple(map(tuple, cl)))
    mid = add_model(key, build_model(fn, tl, cl, tgt))
    P, _, _, _ = mesh(fn); c = (P.min(0) + P.max(0)) / 2; ext = (P.max(0) - P.min(0)) / 2
    A = np.array(M[:9]).reshape(3, 3).T  # columnas -> matriz
    A = np.array([[M[0], M[3], M[6]], [M[1], M[4], M[7]], [M[2], M[5], M[8]]])
    wc = A @ c + np.array(M[9:12]); r = float(np.linalg.norm(A @ ext) if False else np.linalg.norm(ext * np.array([np.linalg.norm(A[:, 0]), np.linalg.norm(A[:, 1]), np.linalg.norm(A[:, 2])])))
    insts.append((mid, M, wc, r))
print("instancias", len(insts), "modelos", len(models))

# ---- pera (jugador): origen en el centro de la base
PM = {i[0]: i for i in D["inst"] if i[0].startswith("mesh_id")}
Pb = mesh("mesh_id32.001_153.obj")[0]; C0 = np.array([(Pb[:, 0].min() + Pb[:, 0].max()) / 2, Pb[:, 1].min(), (Pb[:, 2].min() + Pb[:, 2].max()) / 2])
pear_models = []
for fn, tgt in (("mesh_id32.001_153.obj", 700), ("mesh_id51.001_182.obj", 140), ("mesh_id47.001_180.obj", 60)):
    i = PM[fn]; M = i[3]; A = np.array([[M[0], M[3], M[6]], [M[1], M[4], M[7]], [M[2], M[5], M[8]]])
    def xf(P, N, A=A): return (P - C0) @ A.T, N @ (A / np.linalg.norm(A, axis=0)).T
    pear_models.append(add_model(("pear", fn), build_model(fn, i[1], i[2], tgt, xf)))
# gema y bola
gem = add_model(("gem",), build_model(D["gemMesh"], [None], [[1, .9, .2]], 40))
def ball(col=(.86,.62,.1)):
    t = (1 + 5 ** .5) / 2; V = np.array([[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]], float)
    V /= np.linalg.norm(V, axis=1)[:, None]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    return [make_part(V, V, np.zeros((12, 2)), np.array(F), -1, list(col))]
ballm = add_model(("ball",), ball)
shadowm = add_model(("shadow",), lambda: ball((.12,.36,.12)))
print("partes", len(parts), "triangulos totales", sum(len(p["I"]) // 3 for p in parts), "texturas", len(tex_order))

# ---- escritura
def arr(a, fmt): return ",".join(fmt % v for v in a)
o = ["/* Generado por make_assets.py. No editar a mano. */", "#ifndef ASSETS_H", "#define ASSETS_H",
     "typedef struct { const float *pos; const float *uv; const unsigned char *col; const unsigned short *idx; unsigned short nv, ni; signed char tex; } Part;",
     "typedef struct { unsigned short first, n; } Model;",
     "typedef struct { unsigned short model; float m[12]; float c[3]; float r; } Inst;",
     "typedef struct { unsigned short w, h; const unsigned short *px; } TexData;"]
for k, name in enumerate(tex_order):
    _, size, v = textures[name]; o.append("static const unsigned short TEXPX%d[%d]={%s};" % (k, len(v), arr(v, "%d")))
o.append("static const TexData TEXTURES[%d]={%s};" % (len(tex_order), ",".join("{%d,%d,TEXPX%d}" % (textures[n][1][0], textures[n][1][1], k) for k, n in enumerate(tex_order))))
for k, p in enumerate(parts):
    o.append("static const float PP%d[]={%s};static const float PU%d[]={%s};static const unsigned char PC%d[]={%s};static const unsigned short PI%d[]={%s};" % (
        k, arr(p["P"].reshape(-1), "%.3f"), k, arr(p["U"].reshape(-1), "%.3f"), k, arr(p["C"].reshape(-1), "%d"), k, arr(p["I"], "%d")))
o.append("static const Part PARTS[%d]={%s};" % (len(parts), ",".join("{PP%d,PU%d,PC%d,PI%d,%d,%d,%d}" % (k, k, k, k, len(p["P"]), len(p["I"]), p["tex"]) for k, p in enumerate(parts))))
o.append("static const Model MODELS[%d]={%s};" % (len(models), ",".join("{%d,%d}" % m for m in models)))
o.append("#define NINST %d" % len(insts))
o.append("static const Inst INSTS[NINST]={" + ",".join("{%d,{%s},{%.2f,%.2f,%.2f},%.2f}" % (m, arr(M, "%.4f"), *wc, r) for m, M, wc, r in insts) + "};")
o.append("#define MODEL_GEM %d\n#define MODEL_BALL %d\n#define MODEL_SHADOW %d\n#define PEAR_N 3\nstatic const int PEAR_MODELS[3]={%d,%d,%d};" % (gem, ballm, shadowm, *pear_models))
o.append("#endif")
open(os.path.join(SRC, "assets.h"), "w").write("\n".join(o))

# ---- colisiones extra en level.h
base = open(os.path.join(HERE, "level_base.h")).read()
extra = ["  {{%.3f,%.3f,%.3f},{%.3f,%.3f,%.3f},{%s},0,0}," % (*b[0:3], *b[3:6], ",".join("%.4f" % v for v in b[6:15])) for b in D["obb"]]
nb = int(re.search(r"#define NBOX (\d+)", base).group(1))
base = base.replace("#define NBOX %d" % nb, "#define NBOX %d\n#define NBOXDRAW %d" % (nb + len(extra), nb), 1)
j = base.index("};", base.index("LBOXES"))
base = base[:j] + "\n".join(extra) + "\n" + base[j:]
open(os.path.join(SRC, "level.h"), "w").write(base)
print("cajas", nb, "+", len(extra), " assets.h", os.path.getsize(os.path.join(SRC, "assets.h")) // 1024, "KB")
