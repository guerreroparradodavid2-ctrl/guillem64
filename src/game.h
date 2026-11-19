/* Logica del juego (sin libdragon): fisica, colisiones, gemas. */
#ifndef GAME_H
#define GAME_H
#include <math.h>
#include <string.h>
#include "level.h"

#define PR 0.30f          /* radio del jugador */
#define PH 1.75f          /* altura */
#define EYE 1.60f
#define GRAV 24.0f
#define JUMPV 9.0f
#define TRAMPV 20.0f

typedef struct {
    float p[3];           /* pies */
    float vy;
    float yaw, pitch;
    int grounded;
    int gems;
    int dead_count;
    int won;
    unsigned char got[NGEM];
    float t;
} Player;

static void game_reset(Player *pl) {
    pl->p[0] = LSTART[0]; pl->p[1] = LSTART[1] + 0.2f; pl->p[2] = LSTART[2];
    pl->vy = 0; pl->grounded = 0;
}

static void game_init(Player *pl) {
    memset(pl, 0, sizeof *pl);
    game_reset(pl);
    pl->yaw = 3.14159265f;
}

/* Empuja una esfera (c, r) fuera de una caja orientada. Devuelve 1 si hubo contacto
   y deja la normal de empuje en n. */
static int sphere_vs_box(const LBox *b, float c[3], float r, float n[3]) {
    float d[3] = { c[0]-b->p[0], c[1]-b->p[1], c[2]-b->p[2] };
    float l[3];
    for (int j = 0; j < 3; j++) l[j] = b->r[0*3+j]*d[0] + b->r[1*3+j]*d[1] + b->r[2*3+j]*d[2];  /* R^T d */
    float q[3];
    for (int j = 0; j < 3; j++) q[j] = l[j] > b->h[j] ? b->h[j] : (l[j] < -b->h[j] ? -b->h[j] : l[j]);
    float dl[3] = { l[0]-q[0], l[1]-q[1], l[2]-q[2] };
    float d2 = dl[0]*dl[0] + dl[1]*dl[1] + dl[2]*dl[2];
    float nl[3], push;
    if (d2 > 1e-8f) {
        if (d2 >= r*r) return 0;
        float dist = sqrtf(d2);
        for (int j = 0; j < 3; j++) nl[j] = dl[j] / dist;
        push = r - dist;
    } else {
        /* centro dentro de la caja: salir por la cara mas cercana */
        int bj = 0; float best = 1e9f, sg = 1;
        for (int j = 0; j < 3; j++) {
            float pen = b->h[j] - fabsf(l[j]);
            if (pen < best) { best = pen; bj = j; sg = l[j] >= 0 ? 1.f : -1.f; }
        }
        nl[0] = nl[1] = nl[2] = 0; nl[bj] = sg;
        push = best + r;
    }
    for (int i = 0; i < 3; i++) n[i] = b->r[i*3+0]*nl[0] + b->r[i*3+1]*nl[1] + b->r[i*3+2]*nl[2];   /* R nl */
    for (int i = 0; i < 3; i++) c[i] += n[i] * push;
    return 1;
}

static int box_near(const LBox *b, const float p[3]) {
    float m = b->h[0] > b->h[1] ? b->h[0] : b->h[1];
    if (b->h[2] > m) m = b->h[2];
    m = m * 1.74f + 3.f;
    return fabsf(p[0]-b->p[0]) < m && fabsf(p[1]-b->p[1]) < m && fabsf(p[2]-b->p[2]) < m;
}

/* un paso de simulacion. mx = lateral (-1..1), mz = adelante (-1..1) */
static void game_step(Player *pl, float dt, float mx, float mz, int jump, int sprint) {
    pl->t += dt;
    float sp = sprint ? 9.0f : 5.5f;
    float sy = sinf(pl->yaw), cy = cosf(pl->yaw);
    float fx = -sy, fz = -cy, rx = cy, rz = -sy;
    float wx = (fx*mz + rx*mx) * sp, wz = (fz*mz + rz*mx) * sp;
    if (jump && pl->grounded) { pl->vy = JUMPV; pl->grounded = 0; }
    pl->vy -= GRAV * dt;
    if (pl->vy < -40.f) pl->vy = -40.f;
    pl->p[0] += wx * dt; pl->p[1] += pl->vy * dt; pl->p[2] += wz * dt;

    int grounded = 0, tramp = 0, kill = 0, fin = 0;
    for (int it = 0; it < 3; it++) {
        for (int k = 0; k < 3; k++) {
            float cy2 = k == 0 ? PR : (k == 1 ? PH * 0.5f : PH - PR);
            float c[3] = { pl->p[0], pl->p[1] + cy2, pl->p[2] };
            for (int i = 0; i < NBOX; i++) {
                const LBox *b = &LBOXES[i];
                if (!box_near(b, c)) continue;
                float n[3];
                float c0[3] = { c[0], c[1], c[2] };
                if (!sphere_vs_box(b, c0, PR, n)) continue;
                if (b->kind == 1) { kill = 1; continue; }
                if (b->kind == 3) fin = 1;
                if (b->trig) continue;
                c[0] = c0[0]; c[1] = c0[1]; c[2] = c0[2];
                if (n[1] > 0.55f) { grounded = 1; if (b->kind == 2) tramp = 1; }
                if (n[1] < -0.55f && pl->vy > 0) pl->vy = 0;
            }
            pl->p[0] = c[0]; pl->p[1] = c[1] - cy2; pl->p[2] = c[2];
        }
    }
    if (grounded && pl->vy < 0) pl->vy = 0;
    if (tramp) { pl->vy = TRAMPV; grounded = 0; }
    pl->grounded = grounded;
    if (fin) pl->won = 1;

    for (int i = 0; i < NKILL; i++) {
        float dx = pl->p[0]-LKILL[i][0], dy = pl->p[1]+0.9f-LKILL[i][1], dz = pl->p[2]-LKILL[i][2];
        if (dx*dx + dy*dy + dz*dz < (LKILL[i][3]+PR)*(LKILL[i][3]+PR)) kill = 1;
    }
    if (pl->p[1] < 0.f) kill = 1;
    if (kill) { pl->dead_count++; game_reset(pl); }

    for (int i = 0; i < NGEM; i++) {
        if (pl->got[i]) continue;
        float dx = pl->p[0]-LGEMS[i][0], dy = pl->p[1]+0.9f-LGEMS[i][1], dz = pl->p[2]-LGEMS[i][2];
        if (dx*dx + dy*dy + dz*dz < 1.4f*1.4f) { pl->got[i] = 1; pl->gems++; }
    }
}
#endif
