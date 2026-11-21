/* Guillem the Frutem Adventure - port no oficial para Nintendo 64 (libdragon).
   Render por SOFTWARE (solo CPU): no usa RSP ni RDP, asi que funciona en emuladores "normales". */
#include <libdragon.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "assets.h"
#include "music.h"
#include "title.h"

#define W 128
#define H 96
#define NEAR 0.3f
#define FOCAL 70.2f          /* (H/2)/tan(fov/2) */
#define PEAR_ROT 1.5708f

static Player pl;
static unsigned short cbuf_off[W * H];      /* imagen fuera de pantalla (menu de titulo) */
static unsigned short *cbuf = cbuf_off;       /* en juego apunta directo al framebuffer */
static float zbuf[W * H];
static float face_a = 3.14159265f, last_x = 1e9f, last_z = 1e9f;
static unsigned short SKY;

typedef struct { float x, y, z, u, v, r, g, b; } VV;                /* vertice en espacio camara */
typedef struct { float x, y, iz, uz, vz, r, g, b; } SV;             /* vertice en pantalla */

static float cam_p[3], cam_r[3], cam_u[3], cam_f[3];

static void set_camera(const float eye[3], const float tgt[3]) {
    float f[3] = { tgt[0]-eye[0], tgt[1]-eye[1], tgt[2]-eye[2] };
    float l = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= l; f[1] /= l; f[2] /= l;
    float r[3] = { -f[2], 0, f[0] }; l = sqrtf(r[0]*r[0] + r[2]*r[2]);
    if (l < 1e-6f) { l = 1; }
    r[0] /= l; r[2] /= l;
    float u[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };
    for (int i = 0; i < 3; i++) { cam_p[i] = eye[i]; cam_r[i] = r[i]; cam_u[i] = u[i]; cam_f[i] = f[i]; }
}


/* ---------- sonido: musica PCM + efectos sinteticos, solo CPU (sin RSP) ---------- */
#define ARATE 22050
typedef struct { float f0, f1, vol; int delay, dur, pos; float ph; } Voice;
static Voice voices[6];
static int audio_ok = 0, mpos = 0, music_on = 1;
static float mpf = 0;
static void sfx_add(float f0, float f1, float vol, int delay_ms, int dur_ms) {
    for (int i = 0; i < 6; i++) if (voices[i].dur == 0) {
        voices[i].f0 = f0; voices[i].f1 = f1; voices[i].vol = vol; voices[i].delay = delay_ms * ARATE / 1000;
        voices[i].dur = dur_ms * ARATE / 1000; voices[i].pos = 0; voices[i].ph = 0; return;
    }
}
static void sfx_play(int k) {
    if (k == 0) { sfx_add(523, 523, 0.18f, 0, 90); sfx_add(784, 784, 0.18f, 80, 160); }
    else if (k == 1) sfx_add(300, 700, 0.12f, 0, 150);
    else if (k == 2) sfx_add(400, 70, 0.2f, 0, 450);
    else { sfx_add(523, 523, 0.18f, 0, 200); sfx_add(659, 659, 0.18f, 150, 200); sfx_add(784, 784, 0.18f, 300, 200); sfx_add(1047, 1047, 0.18f, 450, 400); }
}
static void audio_pump(void) {
    if (!audio_ok || !audio_can_write()) return;
    short *buf = audio_write_begin();
    int n = audio_get_buffer_length();
    const float step = (float)MUSIC_RATE / ARATE;
    for (int i = 0; i < n; i++) {
        float m = 0;
        if (music_on) {
            m = MUSIC[mpos] * 0.55f;
            mpf += step; while (mpf >= 1.f) { mpf -= 1.f; mpos++; if (mpos >= MUSIC_LEN) mpos = 0; }
        }
        float sfx = 0;
        for (int v = 0; v < 6; v++) {
            Voice *q = &voices[v];
            if (q->dur == 0) continue;
            if (q->delay > 0) { q->delay--; continue; }
            float t = (float)q->pos / q->dur;
            float f = q->f0 + (q->f1 - q->f0) * t;
            q->ph += f / ARATE; if (q->ph >= 1.f) q->ph -= 1.f;
            sfx += (q->ph < 0.5f ? 1.f : -1.f) * q->vol * (1.f - t) * 12000.f;
            if (++q->pos >= q->dur) q->dur = 0;
        }
        float o = m + sfx;
        if (o > 32000.f) { o = 32000.f; }
        if (o < -32000.f) { o = -32000.f; }
        short sv = (short)o;
        buf[2 * i] = sv; buf[2 * i + 1] = sv;
    }
    audio_write_end();
}

/* ---------- rasterizador ---------- */
/* acota un float antes de pasarlo a int: la FPU de la N64 lanza excepcion si se desborda */
#define CLAMPF(v) fminf(fmaxf((v), -1000000.f), 1000000.f)
/* triangulos diminutos del mundo se descartan (casi no cubren pixeles); la pera usa un umbral pequeno */
static float MINA = 3.f;
static void raster(const SV *a, const SV *b, const SV *c, const unsigned short *tex, int tw, int th, int wm, int hm) {
    float area = (b->x-a->x)*(c->y-a->y) - (c->x-a->x)*(b->y-a->y);
    if (fabsf(area) < MINA) return;
    if (area < 0) { const SV *t = b; b = c; c = t; area = -area; }
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    minx = CLAMPF(minx); maxx = CLAMPF(maxx); miny = CLAMPF(miny); maxy = CLAMPF(maxy);
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > W - 1) { x1 = W - 1; }
    if (y1 > H - 1) { y1 = H - 1; }
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.f / area;
    /* gradientes de los atributos (plano) */
    float e0dx = -(b->y-a->y), e0dy = (b->x-a->x);       /* E(a,b,p) */
    float e1dx = -(c->y-b->y), e1dy = (c->x-b->x);       /* E(b,c,p) */
    float e2dx = -(a->y-c->y), e2dy = (a->x-c->x);       /* E(c,a,p) */
    float px = x0 + 0.5f, py = y0 + 0.5f;
    float e0r = (b->x-a->x)*(py-a->y) - (b->y-a->y)*(px-a->x);
    float e1r = (c->x-b->x)*(py-b->y) - (c->y-b->y)*(px-b->x);
    float e2r = (a->x-c->x)*(py-c->y) - (a->y-c->y)*(px-c->x);
    /* baricentricas: w0=e1/area (a), w1=e2/area (b), w2=e0/area (c) */
    float fa[6] = { a->iz, a->uz, a->vz, a->r, a->g, a->b };
    float fb[6] = { b->iz, b->uz, b->vz, b->r, b->g, b->b };
    float fc[6] = { c->iz, c->uz, c->vz, c->r, c->g, c->b };
    float fdx[6], fdy[6], fr[6];
    for (int k = 0; k < 6; k++) {
        fdx[k] = (fa[k]*e1dx + fb[k]*e2dx + fc[k]*e0dx) * inv;
        fdy[k] = (fa[k]*e1dy + fb[k]*e2dy + fc[k]*e0dy) * inv;
        fr[k]  = (fa[k]*e1r  + fb[k]*e2r  + fc[k]*e0r ) * inv;
    }
    /* inversas de los gradientes de borde: el rango de x de cada fila se calcula, no se recorre toda la caja */
    float i0 = e0dx != 0.f ? 1.f / e0dx : 0.f, i1 = e1dx != 0.f ? 1.f / e1dx : 0.f, i2 = e2dx != 0.f ? 1.f / e2dx : 0.f;
    int maxt = x1 - x0;
    float tw_f = (float)tw, th_f = (float)th;
    float dz = fdx[0];
    for (int y = y0; y <= y1; y++, e0r += e0dy, e1r += e1dy, e2r += e2dy, fr[0] += fdy[0], fr[1] += fdy[1], fr[2] += fdy[2], fr[3] += fdy[3], fr[4] += fdy[4], fr[5] += fdy[5]) {
        float tminf = 0.f, tmaxf = (float)maxt;
        if (e0dx > 0.f) { float tf = ceilf(-e0r * i0); if (tf > tminf) tminf = tf; } else if (e0dx < 0.f) { float tf = floorf(-e0r * i0); if (tf < tmaxf) tmaxf = tf; } else if (e0r < 0.f) continue;
        if (e1dx > 0.f) { float tf = ceilf(-e1r * i1); if (tf > tminf) tminf = tf; } else if (e1dx < 0.f) { float tf = floorf(-e1r * i1); if (tf < tmaxf) tmaxf = tf; } else if (e1r < 0.f) continue;
        if (e2dx > 0.f) { float tf = ceilf(-e2r * i2); if (tf > tminf) tminf = tf; } else if (e2dx < 0.f) { float tf = floorf(-e2r * i2); if (tf < tmaxf) tmaxf = tf; } else if (e2r < 0.f) continue;
        if (!(tminf <= tmaxf)) continue;
        int tmin = (int)tminf, tmax = (int)tmaxf;
        float *zr = &zbuf[y * W + x0]; unsigned short *cr = &cbuf[y * W + x0];
        float f0 = fr[0] + dz * (float)tmin;
        int t = tmin;
        if (tex) {
            while (t <= tmax) {
                int tn = t + 8; int last = 0;
                if (tn >= tmax) { tn = tmax; last = 1; }
                float fzA = fr[0] + dz * (float)t, fzB = fr[0] + dz * (float)tn; if (fzA < 1e-5f) fzA = 1e-5f; if (fzB < 1e-5f) fzB = 1e-5f; float zA = 1.f / fzA, zB = 1.f / fzB;
                float uA = CLAMPF((fr[1] + fdx[1] * (float)t) * zA * tw_f), vA = CLAMPF((fr[2] + fdx[2] * (float)t) * zA * th_f);
                float uB = CLAMPF((fr[1] + fdx[1] * (float)tn) * zB * tw_f), vB = CLAMPF((fr[2] + fdx[2] * (float)tn) * zB * th_f);
                int len = tn - t;
                float du = len ? (uB - uA) / (float)len : 0.f, dv = len ? (vB - vA) / (float)len : 0.f;
                int end = last ? tn + 1 : tn;
                for (; t < end; t++, f0 += dz, uA += du, vA += dv) {
                    if (f0 > zr[t]) {
                        zr[t] = f0;
                        float ft = (float)t;
                        int R = (int)CLAMPF(fr[3] + fdx[3] * ft), G = (int)CLAMPF(fr[4] + fdx[4] * ft), B = (int)CLAMPF(fr[5] + fdx[5] * ft);
                        unsigned short tc = tex[(((int)vA) & hm) * tw + (((int)uA) & wm)];
                        if (R < 0) { R = 0; }
                        if (G < 0) { G = 0; }
                        if (B < 0) { B = 0; }
                        R = (((tc >> 11) & 31) * R) >> 7; G = (((tc >> 6) & 31) * G) >> 7; B = (((tc >> 1) & 31) * B) >> 7;
                        if (R > 31) { R = 31; }
                        if (G > 31) { G = 31; }
                        if (B > 31) { B = 31; }
                        cr[t] = (R << 11) | (G << 6) | (B << 1) | 1;
                    }
                }
            }
        } else {
            for (; t <= tmax; t++, f0 += dz) {
                if (f0 > zr[t]) {
                    zr[t] = f0;
                    float ft = (float)t;
                    int R = (int)CLAMPF(fr[3] + fdx[3] * ft), G = (int)CLAMPF(fr[4] + fdx[4] * ft), B = (int)CLAMPF(fr[5] + fdx[5] * ft);
                    if (R < 0) { R = 0; }
                    if (R > 255) { R = 255; }
                    if (G < 0) { G = 0; }
                    if (G > 255) { G = 255; }
                    if (B < 0) { B = 0; }
                    if (B > 255) { B = 255; }
                    cr[t] = ((R >> 3) << 11) | ((G >> 3) << 6) | ((B >> 3) << 1) | 1;
                }
            }
        }
    }
}

static void project(const VV *v, SV *o) {
    float iz = 1.f / v->z;
    o->x = W * 0.5f + v->x * iz * FOCAL; o->y = H * 0.5f - v->y * iz * FOCAL; o->iz = iz;
    o->uz = v->u * iz; o->vz = v->v * iz; o->r = v->r; o->g = v->g; o->b = v->b;
}

static VV lerp_vv(const VV *a, const VV *b, float t) {
    VV o; o.x = a->x + (b->x-a->x)*t; o.y = a->y + (b->y-a->y)*t; o.z = a->z + (b->z-a->z)*t;
    o.u = a->u + (b->u-a->u)*t; o.v = a->v + (b->v-a->v)*t;
    o.r = a->r + (b->r-a->r)*t; o.g = a->g + (b->g-a->g)*t; o.b = a->b + (b->b-a->b)*t; return o;
}

/* un triangulo en espacio camara: descarte, recorte contra el plano cercano y raster */
static void submit(const VV *a, const VV *b, const VV *c, const unsigned short *tex, int tw, int th) {
    if (a->z > 85 && b->z > 85 && c->z > 85) return;
    if (a->z < NEAR && b->z < NEAR && c->z < NEAR) return;
    if (a->x > 1.9f*a->z && b->x > 1.9f*b->z && c->x > 1.9f*c->z) return;
    if (a->x < -1.9f*a->z && b->x < -1.9f*b->z && c->x < -1.9f*c->z) return;
    if (a->y > 0.75f*a->z && b->y > 0.75f*b->z && c->y > 0.75f*c->z) return;
    if (a->y < -0.75f*a->z && b->y < -0.75f*b->z && c->y < -0.75f*c->z) return;
    int wm = tw - 1, hm = th - 1;
    const VV *in[3] = { a, b, c };
    VV poly[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        const VV *p = in[i], *q = in[(i + 1) % 3];
        int pi = p->z >= NEAR, qi = q->z >= NEAR;
        if (pi) poly[n++] = *p;
        if (pi != qi) { float t = (NEAR - p->z) / (q->z - p->z); poly[n++] = lerp_vv(p, q, t); }
    }
    if (n < 3) return;
    SV s[4];
    for (int i = 0; i < n; i++) project(&poly[i], &s[i]);
    raster(&s[0], &s[1], &s[2], tex, tw, th, wm, hm);
    if (n == 4) raster(&s[0], &s[2], &s[3], tex, tw, th, wm, hm);
}

/* ---------- modelos ---------- */
static VV tv[512];
static void draw_model(int mid, const float m[12]) {
    const Model *md = &MODELS[mid];
    audio_pump();
    /* matriz combinada: vista * modelo */
    float cm[12];
    for (int c = 0; c < 3; c++) {
        const float *q = &m[c * 3];
        cm[c*3+0] = q[0]*cam_r[0] + q[1]*cam_r[1] + q[2]*cam_r[2];
        cm[c*3+1] = q[0]*cam_u[0] + q[1]*cam_u[1] + q[2]*cam_u[2];
        cm[c*3+2] = q[0]*cam_f[0] + q[1]*cam_f[1] + q[2]*cam_f[2];
    }
    float d[3] = { m[9]-cam_p[0], m[10]-cam_p[1], m[11]-cam_p[2] };
    cm[9] = d[0]*cam_r[0] + d[1]*cam_r[1] + d[2]*cam_r[2];
    cm[10] = d[0]*cam_u[0] + d[1]*cam_u[1] + d[2]*cam_u[2];
    cm[11] = d[0]*cam_f[0] + d[1]*cam_f[1] + d[2]*cam_f[2];
    for (int k = 0; k < md->n; k++) {
        const Part *p = &PARTS[md->first + k];
        for (int i = 0; i < p->nv; i++) {
            float x = p->pos[i*3], y = p->pos[i*3+1], z = p->pos[i*3+2];
            VV *o = &tv[i];
            o->x = cm[0]*x + cm[3]*y + cm[6]*z + cm[9];
            o->y = cm[1]*x + cm[4]*y + cm[7]*z + cm[10];
            o->z = cm[2]*x + cm[5]*y + cm[8]*z + cm[11];
            o->u = p->uv[i*2]; o->v = p->uv[i*2+1];
            o->r = p->col[i*4]; o->g = p->col[i*4+1]; o->b = p->col[i*4+2];
        }
        const unsigned short *tx = 0; int tw = 1, th = 1;
        if (p->tex >= 0) { tx = TEXTURES[p->tex].px; tw = TEXTURES[p->tex].w; th = TEXTURES[p->tex].h; }
        for (int i = 0; i + 2 < p->ni; i += 3)
            submit(&tv[p->idx[i]], &tv[p->idx[i+1]], &tv[p->idx[i+2]], tx, tw, th);
    }
}

/* ---------- cajas ---------- */
static const int CF[6][4] = { {3,2,6,7}, {0,1,5,4}, {1,2,6,5}, {0,3,7,4}, {4,5,6,7}, {0,1,2,3} };
static const float CS[6] = { 0.85f, 0.65f, 0.80f, 0.70f, 1.00f, 0.50f };
static void box_color(const LBox *b, int i, float *r, float *g, float *bl) {
    switch (b->kind) {
        case 1: *r = 0.9f; *g = 0.15f; *bl = 0.15f; return;
        case 2: *r = 1.0f; *g = 0.85f; *bl = 0.1f; return;
        case 3: *r = 0.7f; *g = 0.3f; *bl = 0.95f; return;
        case 4: *r = 0.2f; *g = 0.8f; *bl = 0.9f; return;
        case 5: *r = 0.25f; *g = 0.6f; *bl = 0.25f; return;
        default: break;
    }
    float t = (b->p[1] - 15.f) / 105.f;
    if (t < 0) { t = 0; }
    if (t > 1) { t = 1; }
    float v = 0.55f + 0.25f * (float)((i * 37) % 5) / 4.f;
    *r = v * (0.7f + 0.3f * t); *g = v * 0.75f; *bl = v * (1.0f - 0.3f * t);
}
static void draw_box(const LBox *b, float cr, float cg, float cb, int tex, float tile) {
    VV c[8]; float lu[8], lv_[8], lw[8];
    for (int i = 0; i < 8; i++) {
        float lx = ((i & 1) ^ ((i >> 1) & 1) ? 1 : -1) * b->h[0];
        float ly = (i >> 2 ? 1 : -1) * b->h[1];
        float lz = ((i >> 1) & 1 ? 1 : -1) * b->h[2];
        float wx = b->r[0]*lx + b->r[1]*ly + b->r[2]*lz + b->p[0] - cam_p[0];
        float wy = b->r[3]*lx + b->r[4]*ly + b->r[5]*lz + b->p[1] - cam_p[1];
        float wz = b->r[6]*lx + b->r[7]*ly + b->r[8]*lz + b->p[2] - cam_p[2];
        c[i].x = wx*cam_r[0] + wy*cam_r[1] + wz*cam_r[2];
        c[i].y = wx*cam_u[0] + wy*cam_u[1] + wz*cam_u[2];
        c[i].z = wx*cam_f[0] + wy*cam_f[1] + wz*cam_f[2];
        c[i].u = c[i].v = 0; lu[i] = lx; lv_[i] = ly; lw[i] = lz;
    }
    const unsigned short *tx = 0; int tw = 1, th = 1;
    if (tex >= 0) { tx = TEXTURES[tex].px; tw = TEXTURES[tex].w; th = TEXTURES[tex].h; }
    /* cara f: 4 vertices consecutivos por el orden (lx,lz): 0(-,-) 1(+,-) 2(+,+) 3(-,+) */
    for (int f = 0; f < 6; f++) {
        VV q[4];
        for (int k = 0; k < 4; k++) {
            int ci = CF[f][k]; q[k] = c[ci];
            q[k].r = cr*CS[f]*255; q[k].g = cg*CS[f]*255; q[k].b = cb*CS[f]*255;
            if (f <= 1) { q[k].u = lu[ci] / tile; q[k].v = lv_[ci] / tile; }
            else if (f <= 3) { q[k].u = lw[ci] / tile; q[k].v = lv_[ci] / tile; }
            else { q[k].u = lu[ci] / tile; q[k].v = lw[ci] / tile; }
        }
        submit(&q[0], &q[1], &q[2], tx, tw, th); submit(&q[0], &q[2], &q[3], tx, tw, th);
    }
}

/* ---------- escena ---------- */
static float cam_d = 4.2f;
static int cam_hit(const float q[3]) {
    for (int i = 0; i < NBOX; i++) {
        const LBox *b = &LBOXES[i];
        if (b->trig || b->kind == 1 || b->kind == 3 || b->kind == 5) continue;
        if (!box_near(b, q)) continue;
        float d[3] = { q[0] - b->p[0], q[1] - b->p[1], q[2] - b->p[2] };
        int ok = 1;
        for (int j = 0; j < 3; j++) {
            float l = b->r[j] * d[0] + b->r[3 + j] * d[1] + b->r[6 + j] * d[2];
            if (fabsf(l) > b->h[j] + 0.25f) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}
static unsigned t_box, t_inst, t_pear, t_cam, t_step, t_loop;   /* microsegundos del ultimo fotograma (diagnostico) */
static void draw_world(void) {
    MINA = 3.f;
    uint64_t T0 = get_ticks_us();
    float e[3] = { pl.p[0], pl.p[1] + EYE, pl.p[2] };
    float cp = cosf(pl.pitch), sp = sinf(pl.pitch);
    float f[3] = { -sinf(pl.yaw) * cp, sp, -cosf(pl.yaw) * cp };
    const float CDMAX = 4.2f;
    float want = CDMAX;
    for (float sd = 0.4f; sd <= CDMAX; sd += 0.3f) {
        float q[3] = { e[0] - f[0] * sd, e[1] - f[1] * sd + 0.6f * sd / CDMAX, e[2] - f[2] * sd };
        if (cam_hit(q)) { want = sd - 0.5f; if (want < 0.6f) want = 0.6f; break; }
    }
    cam_d += (want - cam_d) * (want < cam_d ? 0.6f : 0.1f);
    float CD = cam_d;
    float eye[3] = { e[0] - f[0] * CD, e[1] - f[1] * CD + 0.6f * CD / CDMAX, e[2] - f[2] * CD };
    float tgt[3] = { e[0], e[1] - 0.2f, e[2] };
    set_camera(eye, tgt);

    uint64_t T1 = get_ticks_us();
    for (int i = 0; i < NBOXDRAW; i++) {
        const LBox *b = &LBOXES[i];
        float big = b->h[0] > b->h[2] ? b->h[0] : b->h[2];
        if (big > 1000.f && b->kind != 5) continue;
        float dx = b->p[0] - e[0], dy = b->p[1] - e[1], dz = b->p[2] - e[2];
        float lim = 60.f + big;
        if (b->kind != 5 && dx*dx + dy*dy + dz*dz > lim * lim) continue;
        float cr, cg, cb; box_color(b, i, &cr, &cg, &cb);
        int bt = BOXTEX[i];
        if (bt >= 0) { cr = cg = cb = 1.f; }
        else if (BOXCOL[i][0] >= 0 && (b->kind == 0 || b->kind == 4)) { cr = BOXCOL[i][0] / 255.f; cg = BOXCOL[i][1] / 255.f; cb = BOXCOL[i][2] / 255.f; }
        if (bt >= 0 && b->kind == 5) { cr = 0.76f; cg = 0.86f; cb = 0.68f; }
        if (bt >= 0) { cr *= 0.6f; cg *= 0.6f; cb *= 0.6f; }
        draw_box(b, cr, cg, cb, bt, b->kind == 5 ? 5.f : 3.f);
    }
    uint64_t T2 = get_ticks_us();
    for (int i = 0; i < NINST; i++) {
        const Inst *n = &INSTS[i];
        float dx = n->c[0] - eye[0], dy = n->c[1] - eye[1], dz = n->c[2] - eye[2];
        float lim = 36.f + n->r;
        if (dx*dx + dy*dy + dz*dz > lim * lim) continue;
        if (dx * cam_f[0] + dy * cam_f[1] + dz * cam_f[2] < -n->r) continue;
        draw_model(n->model, n->m);
    }
    for (int i = 0; i < NGEM; i++) {
        if (pl.got[i]) continue;
        float dx = LGEMS[i][0] - e[0], dy = LGEMS[i][1] - e[1], dz = LGEMS[i][2] - e[2];
        if (dx*dx + dy*dy + dz*dz > 36.f * 36.f) continue;
        float a = pl.t * 2.f, c = cosf(a), s = sinf(a);
        float m[12] = { c, 0, -s, 0, 1, 0, s, 0, c, LGEMS[i][0], LGEMS[i][1] + sinf(pl.t * 3.f + i) * 0.15f, LGEMS[i][2] };
        draw_model(MODEL_GEM, m);
    }
    uint64_t T3 = get_ticks_us();
    /* la pera gira hacia donde te mueves */
    if (last_x < 1e8f) {
        float mx = pl.p[0] - last_x, mz = pl.p[2] - last_z;
        if (mx * mx + mz * mz > 1e-5f) {
            float tg = atan2f(-mx, -mz), d = tg - face_a;
            d = atan2f(sinf(d), cosf(d));
            face_a += d * 0.25f;
        }
    }
    last_x = pl.p[0]; last_z = pl.p[2];
    {
        float a = face_a + PEAR_ROT, c = cosf(a), s = sinf(a);
        float m[12] = { c, 0, -s, 0, 1, 0, s, 0, c, pl.p[0], pl.p[1], pl.p[2] };
        float sm[12] = { .55f, 0, 0, 0, .02f, 0, 0, 0, .55f, pl.p[0], pl.p[1] + .02f, pl.p[2] };
        MINA = 0.3f;
        draw_model(MODEL_SHADOW, sm);
        for (int k = 0; k < PEAR_N; k++) draw_model(PEAR_MODELS[k], m);
        float fw[3] = { -sinf(face_a), 0, -cosf(face_a) };
        for (int q = -1; q <= 1; q += 2) {
            float bm[12] = { .3f, 0, 0, 0, .3f, 0, 0, 0, .3f,
                pl.p[0] - fw[0] * 0.40f + (-fw[2]) * q * 0.24f, pl.p[1] + 0.5f, pl.p[2] - fw[2] * 0.40f + fw[0] * q * 0.24f };
            draw_model(MODEL_BALL, bm);
        }
    }
    { uint64_t T4 = get_ticks_us(); t_cam = (unsigned)(T1 - T0); t_box = (unsigned)(T2 - T1); t_inst = (unsigned)(T3 - T2); t_pear = (unsigned)(T4 - T3); }
}

int main(void) {
    /* titulo: 320x240 con la imagen reescalada por CPU. Juego: el framebuffer es de W x H y lo estira el hardware de video de la N64 */
    resolution_t small = { .width = W, .height = H };
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    joypad_init();
    audio_init(ARATE, 6);
    audio_ok = 1;
    SKY = (((int)(0.45f*31)) << 11) | (((int)(0.72f*31)) << 6) | (31 << 1) | 1;
    game_init(&pl);
    int title = 1, pg = 0, pd = 0, pw = 0; float pv = 0;
    uint64_t last = get_ticks_us(); float fps = 0.f;
    while (1) {
        uint64_t now = get_ticks_us();
        float dt = (float)(now - last) / 1000000.f; last = now;
        if (dt > 0.07f) dt = 0.07f;
        if (dt > 0.0001f) fps += (1.f / dt - fps) * 0.1f;

        joypad_poll();
        joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        float mx = (float)in.stick_x / 70.f, mz = (float)in.stick_y / 70.f;
        if (fabsf(mx) < 0.12f) { mx = 0; }
        if (fabsf(mz) < 0.12f) { mz = 0; }
        if (mx > 1) { mx = 1; }
        if (mx < -1) { mx = -1; }
        if (mz > 1) { mz = 1; }
        if (mz < -1) { mz = -1; }
        if (held.c_left)  pl.yaw += 2.2f * dt;
        if (held.c_right) pl.yaw -= 2.2f * dt;
        if (held.c_up)    pl.pitch += 1.5f * dt;
        if (held.c_down)  pl.pitch -= 1.5f * dt;
        if (pl.pitch > 1.2f) { pl.pitch = 1.2f; }
        if (pl.pitch < -1.2f) { pl.pitch = -1.2f; }
        if (title) {
            pl.yaw += 0.25f * dt;
            if (pressed.start || pressed.a) {
                title = 0; game_init(&pl);
                display_close();
                display_init(small, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
                last = get_ticks_us(); continue;
            }
        } else if (pressed.start) { game_init(&pl); pg = pd = pw = 0; }
        uint64_t TS = get_ticks_us();
        if (!title && !pl.won) game_step(&pl, dt, mx, mz, pressed.a || (held.a && pl.grounded), held.b || held.z);
        t_step = (unsigned)(get_ticks_us() - TS);

        if (!title) {
            if (pl.gems > pg) sfx_play(0);
            if (pl.dead_count > pd) sfx_play(2);
            if (pl.won && !pw) sfx_play(3);
            if (pl.vy > 8.f && pv <= 8.f && !pl.won) sfx_play(1);
            pg = pl.gems; pd = pl.dead_count; pw = pl.won; pv = pl.vy;
        }
        surface_t *disp = display_get();
        cbuf = title ? cbuf_off : (unsigned short *)disp->buffer;
        for (int y = 0; y < H; y++) {
            float tt = (float)y / (H * 0.55f); if (tt > 1.f) tt = 1.f;
            int r = (int)(0.18f * 31 + (0.75f - 0.18f) * 31 * tt), g = (int)(0.44f * 31 + (0.88f - 0.44f) * 31 * tt), b = (int)(0.85f * 31 + (1.0f - 0.85f) * 31 * tt);
            unsigned short sc = (r << 11) | (g << 6) | (b << 1) | 1;
            uint32_t sc2 = ((uint32_t)sc << 16) | sc; uint32_t *row = (uint32_t *)&cbuf[y * W];
            for (int x = 0; x < W / 2; x++) row[x] = sc2;
        }
        memset(zbuf, 0, sizeof zbuf);
        draw_world();

        graphics_set_color(graphics_make_color(255, 255, 255, 255), graphics_make_color(0, 0, 0, 0));
        if (title) {
            static unsigned short xmap[320]; static int xmap_ok;
            if (!xmap_ok) { for (int x = 0; x < 320; x++) xmap[x] = (unsigned short)(x * W / 320); xmap_ok = 1; }
            int lastsy = -1; unsigned short *prev = 0;
            for (int y = 0; y < 240; y++) {
                int sy = y * H / 240;
                unsigned short *dst = (unsigned short *)((uint8_t *)disp->buffer + y * disp->stride);
                if (sy == lastsy) { memcpy(dst, prev, 640); }
                else {
                    const unsigned short *src = &cbuf_off[sy * W];
                    for (int x = 0; x < 320; x += 2) { dst[x] = src[xmap[x]]; dst[x + 1] = src[xmap[x + 1]]; }
                    lastsy = sy; prev = dst;
                }
            }
            int ox = (320 - TITLE_W) / 2, oy = 24;
            for (int y = 0; y < TITLE_H; y++) {
                unsigned short *row = (unsigned short *)((uint8_t *)disp->buffer + (oy + y) * disp->stride);
                for (int x = 0; x < TITLE_W; x++) { unsigned short p = TITLE[y * TITLE_W + x]; if (p) row[ox + x] = p; }
            }
            graphics_draw_text(disp, 112, 200, ((now / 500000) & 1) ? "PULSA START" : "           ");
            graphics_draw_text(disp, 40, 224, "Port no oficial - juego de zacdev");
        } else {
            char tx[96];
            snprintf(tx, sizeof tx, "G%d/%d H%d F%d", pl.gems, NGEM, (int)pl.p[1], (int)(fps + 0.5f));
            graphics_draw_text(disp, 4, 4, tx);
            snprintf(tx, sizeof tx, "C%u B%u I%u P%u", t_cam / 1000, t_box / 1000, t_inst / 1000, t_pear / 1000);
            graphics_draw_text(disp, 4, 14, tx);
            snprintf(tx, sizeof tx, "S%u L%u", t_step / 1000, t_loop / 1000);
            graphics_draw_text(disp, 4, 24, tx);
            if (pl.won) { graphics_draw_text(disp, 8, 40, "COMPLETADO!"); graphics_draw_text(disp, 8, 52, "START=OTRA VEZ"); }
        }
        audio_pump();
        data_cache_hit_writeback_invalidate(disp->buffer, disp->stride * disp->height);
        t_loop = (unsigned)(get_ticks_us() - now);
        display_show(disp);
    }
    return 0;
}
