#!/usr/bin/env python3
"""Genera src/music.h (PCM 11025 Hz mono 16 bit) y src/title.h (pantalla de titulo RGBA5551)."""
import os, subprocess, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.join(HERE, "..", "src")
RATE = 11025
mp3 = os.path.join(HERE, "bgm.mp3")
raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", mp3, "-ac", "1", "-ar", str(RATE), "-f", "s16be", "-"], capture_output=True, check=True).stdout
pcm = np.frombuffer(raw, ">i2").astype(int)
pcm = (pcm * 0.9).astype(int)
with open(os.path.join(SRC, "music.h"), "w") as f:
    f.write("/* generado por make_media.py */\n#define MUSIC_RATE %d\n#define MUSIC_LEN %d\nstatic const short MUSIC[MUSIC_LEN]={" % (RATE, len(pcm)))
    f.write(",".join(map(str, pcm))); f.write("};\n")
im = Image.open(os.path.join(HERE, "title_src.png")).convert("RGBA").crop((0, 8, 181, 114))
w = 256; h = int(im.height * w / im.width)
im = im.resize((w, h), Image.LANCZOS); a = np.array(im).astype(int)
v = np.where(a[..., 3] > 128, ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 3) << 6) | ((a[..., 2] >> 3) << 1) | 1, 0)
with open(os.path.join(SRC, "title.h"), "w") as f:
    f.write("/* generado por make_media.py */\n#define TITLE_W %d\n#define TITLE_H %d\nstatic const unsigned short TITLE[TITLE_W*TITLE_H]={" % (w, h))
    f.write(",".join(map(str, v.reshape(-1)))); f.write("};\n")
print("musica", len(pcm), "muestras", len(pcm) / RATE, "s; titulo", w, h)
