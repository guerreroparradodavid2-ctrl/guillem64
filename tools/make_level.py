#!/usr/bin/env python3
"""nivel.tsv (volcado de Unity) -> src/level.h (cajas, gemas, zonas de muerte).
Uso: python tools/make_level.py tools/nivel.tsv src/level.h
Unity es de mano izquierda; se refleja el eje Z para OpenGL (mano derecha)."""
import math, re, sys

rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8")][1:]

def rotm(q):
    x, y, z, w = q
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]

def mirror(R):          # M R M con M = diag(1,1,-1)
    m = [1, 1, -1]
    return [[R[i][j]*m[i]*m[j] for j in range(3)] for i in range(3)]

boxes, gems, kills = [], [], []
start = None
for r in rows:
    name, x, y, z, q, s, comp = r
    x, y, z = float(x), float(y), float(z)
    q = [float(v) for v in q.split(",")]
    s = [float(v) for v in s.split(",")]
    if "script=SUPERCharacterAIO" in comp:
        start = (x, y, -z); continue
    if "script=CollectableGem" in comp:
        gems.append((x, y, -z)); continue
    if "mesh=Sphere" in comp and "script=morirte" in comp:
        kills.append((x, y, -z, 0.5*max(s))); continue
    if "mesh=Cube" not in comp: continue
    m = re.search(r"box\(([\d.\-]+),([\d.\-]+),([\d.\-]+)\)(T?)", comp)
    bs = [float(m.group(i)) for i in (1, 2, 3)] if m else [1, 1, 1]
    trig = bool(m and m.group(4))
    size = [abs(s[i]*bs[i]) for i in range(3)]
    kind = 0
    if "script=morirte" in comp: kind = 1
    elif "script=final" in comp: kind = 3
    elif "script=Trampolin" in comp: kind = 2
    elif "script=mover" in comp: kind = 4
    R = mirror(rotm(q))
    boxes.append((x, y, -z, size, R, kind, 1 if trig else 0))

# suelo del nivel (el terreno original queda en y ~ 15)
boxes.append((-20, 14.0, 20, [400, 2.0, 400], [[1,0,0],[0,1,0],[0,0,1]], 5, 0))

o = ["/* Generado por make_level.py. No editar a mano. */",
     "#ifndef LEVEL_H", "#define LEVEL_H",
     "typedef struct { float p[3]; float h[3]; float r[9]; unsigned char kind, trig; } LBox;",
     "/* kind: 0 normal, 1 muerte, 2 trampolin, 3 meta, 4 movil, 5 suelo */",
     "#define NBOX %d" % len(boxes), "#define NGEM %d" % len(gems), "#define NKILL %d" % len(kills),
     "static const float LSTART[3] = {%.3ff, %.3ff, %.3ff};" % start,
     "static const LBox LBOXES[NBOX] = {"]
for (x, y, z, size, R, kind, trig) in boxes:
    flat = [R[i][j] for i in range(3) for j in range(3)]
    o.append("  {{%.3ff,%.3ff,%.3ff},{%.3ff,%.3ff,%.3ff},{%s},%d,%d}," % (
        x, y, z, size[0]/2, size[1]/2, size[2]/2, ",".join("%.4ff" % v for v in flat), kind, trig))
o.append("};")
o.append("static const float LGEMS[NGEM][3] = {" + ",".join("{%.3ff,%.3ff,%.3ff}" % g for g in gems) + "};")
o.append("static const float LKILL[NKILL][4] = {" + ",".join("{%.3ff,%.3ff,%.3ff,%.3ff}" % k for k in kills) + "};")
o.append("#endif")
open(sys.argv[2], "w").write("\n".join(o) + "\n")
print("cajas", len(boxes), "gemas", len(gems), "esferas de muerte", len(kills), "inicio", start)
