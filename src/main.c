/* Guillem the Frutem Adventure - port no oficial para Nintendo 64 (libdragon).
   Etapa 1: nivel con cajas, camara en primera persona, gemas, trampolines, muerte y meta. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <stdlib.h>
#include "game.h"
#include "assets.h"
#define PEAR_ROT 1.5708f
static GLuint texid[sizeof(TEXTURES)/sizeof(TEXTURES[0])];
static float face_a = 3.14159265f, last_x = 1e9f, last_z = 1e9f;

static surface_t zbuf;
static Player pl;

static const float CUBE_V[6][4][3] = {
    {{-.5f, .5f,-.5f},{ .5f, .5f,-.5f},{ .5f, .5f, .5f},{-.5f, .5f, .5f}},   /* +Y arriba */
    {{-.5f,-.5f, .5f},{ .5f,-.5f, .5f},{ .5f,-.5f,-.5f},{-.5f,-.5f,-.5f}},   /* -Y abajo */
    {{ .5f,-.5f,-.5f},{ .5f,-.5f, .5f},{ .5f, .5f, .5f},{ .5f, .5f,-.5f}},   /* +X */
    {{-.5f,-.5f, .5f},{-.5f,-.5f,-.5f},{-.5f, .5f,-.5f},{-.5f, .5f, .5f}},   /* -X */
    {{-.5f,-.5f, .5f},{ .5f,-.5f, .5f},{ .5f, .5f, .5f},{-.5f, .5f, .5f}},   /* +Z */
    {{ .5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f, .5f,-.5f},{ .5f, .5f,-.5f}},   /* -Z */
};
static const float SHADE[6] = { 1.00f, 0.50f, 0.80f, 0.70f, 0.85f, 0.65f };

static void draw_cube(float cr, float cg, float cb) {
    for (int f = 0; f < 6; f++) {
        float s = SHADE[f];
        glColor3f(cr * s, cg * s, cb * s);
        glBegin(GL_TRIANGLES);
        for (int t = 0; t < 2; t++) {
            static const int idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int k = 0; k < 3; k++) {
                const float *v = CUBE_V[f][idx[t][k]];
                glVertex3f(v[0], v[1], v[2]);
            }
        }
        glEnd();
    }
}

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
        if (t < 0) t = 0;
        if (t > 1) t = 1;
    float v = 0.55f + 0.25f * (float)((i * 37) % 5) / 4.f;
    *r = v * (0.7f + 0.3f * t); *g = v * 0.75f; *bl = v * (1.0f - 0.3f * t);
}

static void textures_init(void) {
    int n = sizeof(TEXTURES) / sizeof(TEXTURES[0]);
    glGenTextures(n, texid);
    for (int i = 0; i < n; i++) {
        glBindTexture(GL_TEXTURE_2D, texid[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURES[i].w, TEXTURES[i].h, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1_EXT, TEXTURES[i].px);
    }
}

static void draw_model(int mid) {
    const Model *m = &MODELS[mid];
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    for (int k = 0; k < m->n; k++) {
        const Part *p = &PARTS[m->first + k];
        if (p->tex >= 0) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texid[p->tex]); }
        else glDisable(GL_TEXTURE_2D);
        glVertexPointer(3, GL_FLOAT, 0, p->pos);
        glTexCoordPointer(2, GL_FLOAT, 0, p->uv);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, p->col);
        glDrawElements(GL_TRIANGLES, p->ni, GL_UNSIGNED_SHORT, p->idx);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);
}

static void draw_world(float dt) {
    float e[3] = { pl.p[0], pl.p[1] + EYE, pl.p[2] };
    float cp = cosf(pl.pitch), sp = sinf(pl.pitch);
    float f[3] = { -sinf(pl.yaw) * cp, sp, -cosf(pl.yaw) * cp };
    const float CD = 4.2f;
    float cam[3] = { e[0] - f[0] * CD, e[1] - f[1] * CD + 0.6f, e[2] - f[2] * CD };

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = 320.f / 240.f, nr = 0.3f, tp = nr * tanf(0.6f), fr = 200.f;
    glFrustum(-tp * aspect, tp * aspect, -tp, tp, nr, fr);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cam[0], cam[1], cam[2], e[0], e[1] - 0.2f, e[2], 0, 1, 0);

    /* cajas del nivel (sin las colisiones extra de mallas) */
    for (int i = 0; i < NBOXDRAW; i++) {
        const LBox *b = &LBOXES[i];
        float dx = b->p[0] - e[0], dy = b->p[1] - e[1], dz = b->p[2] - e[2];
        float big = b->h[0] > b->h[2] ? b->h[0] : b->h[2];
        float lim = 90.f + big;
        if (b->kind != 5 && dx*dx + dy*dy + dz*dz > lim * lim) continue;
        float cr, cg, cb; box_color(b, i, &cr, &cg, &cb);
        float m[16] = {
            b->r[0], b->r[3], b->r[6], 0,
            b->r[1], b->r[4], b->r[7], 0,
            b->r[2], b->r[5], b->r[8], 0,
            b->p[0], b->p[1], b->p[2], 1 };
        glPushMatrix();
        glMultMatrixf(m);
        glScalef(b->h[0] * 2, b->h[1] * 2, b->h[2] * 2);
        draw_cube(cr, cg, cb);
        glPopMatrix();
    }
    /* mallas del escenario */
    for (int i = 0; i < NINST; i++) {
        const Inst *n = &INSTS[i];
        float dx = n->c[0] - cam[0], dy = n->c[1] - cam[1], dz = n->c[2] - cam[2];
        float lim = 45.f + n->r;
        if (dx*dx + dy*dy + dz*dz > lim * lim) continue;
        if (dx * f[0] + dy * f[1] + dz * f[2] < -n->r) continue;
        const float *q = n->m;
        float m[16] = { q[0], q[1], q[2], 0, q[3], q[4], q[5], 0, q[6], q[7], q[8], 0, q[9], q[10], q[11], 1 };
        glPushMatrix();
        glMultMatrixf(m);
        draw_model(n->model);
        glPopMatrix();
    }
    /* gemas */
    for (int i = 0; i < NGEM; i++) {
        if (pl.got[i]) continue;
        float dx = LGEMS[i][0] - e[0], dy = LGEMS[i][1] - e[1], dz = LGEMS[i][2] - e[2];
        if (dx*dx + dy*dy + dz*dz > 70.f * 70.f) continue;
        glPushMatrix();
        glTranslatef(LGEMS[i][0], LGEMS[i][1] + sinf(pl.t * 3.f + i) * 0.15f, LGEMS[i][2]);
        glRotatef(pl.t * 114.6f, 0, 1, 0);
        draw_model(MODEL_GEM);
        glPopMatrix();
    }
    /* la pera: gira hacia donde te mueves */
    if (last_x < 1e8f) {
        float mx = pl.p[0] - last_x, mz = pl.p[2] - last_z;
        if (mx * mx + mz * mz > 1e-5f) {
            float tg = atan2f(-mx, -mz), d = tg - face_a;
            d = atan2f(sinf(d), cosf(d));
            face_a += d * 0.25f;
        }
    }
    last_x = pl.p[0]; last_z = pl.p[2];
    glPushMatrix();
    glTranslatef(pl.p[0], pl.p[1], pl.p[2]);
    glPushMatrix();
    glRotatef((face_a + PEAR_ROT) * 57.29578f, 0, 1, 0);
    for (int k = 0; k < PEAR_N; k++) draw_model(PEAR_MODELS[k]);
    glPopMatrix();
    float fw[3] = { -sinf(face_a), 0, -cosf(face_a) };
    for (int q = -1; q <= 1; q += 2) {
        glPushMatrix();
        glTranslatef(-fw[0] * 0.40f + (-fw[2]) * q * 0.24f, 0.5f, -fw[2] * 0.40f + fw[0] * q * 0.24f);
        glScalef(0.3f, 0.3f, 0.3f);
        draw_model(MODEL_BALL);
        glPopMatrix();
    }
    glPopMatrix();
    (void)dt;
}

int main(void) {
    debug_init_isviewer();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    zbuf = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
    rdpq_init();
    gl_init();
    joypad_init();
    rdpq_font_t *fnt = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, fnt);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    textures_init();
    glClearColor(0.45f, 0.72f, 1.0f, 1.0f);

    game_init(&pl);
    uint64_t last = get_ticks_us();
    while (1) {
        uint64_t now = get_ticks_us();
        float dt = (float)(now - last) / 1000000.f; last = now;
        if (dt > 0.05f) dt = 0.05f;

        joypad_poll();
        joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        float mx = (float)in.stick_x / 70.f, mz = (float)in.stick_y / 70.f;
        if (fabsf(mx) < 0.12f) mx = 0;
        if (fabsf(mz) < 0.12f) mz = 0;
        if (mx > 1) mx = 1;
        if (mx < -1) mx = -1;
        if (mz > 1) mz = 1;
        if (mz < -1) mz = -1;
        if (held.c_left)  pl.yaw += 2.2f * dt;
        if (held.c_right) pl.yaw -= 2.2f * dt;
        if (held.c_up)    pl.pitch += 1.5f * dt;
        if (held.c_down)  pl.pitch -= 1.5f * dt;
        if (pl.pitch > 1.2f) pl.pitch = 1.2f;
        if (pl.pitch < -1.2f) pl.pitch = -1.2f;
        if (pressed.start) { game_init(&pl); }
        if (!pl.won) game_step(&pl, dt, mx, mz, pressed.a || (held.a && pl.grounded), held.b || held.z);

        surface_t *disp = display_get();
        rdpq_attach(disp, &zbuf);
        gl_context_begin();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_world(dt);
        gl_context_end();

        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 12, 18, "GEMAS %d/%d", pl.gems, NGEM);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 12, 30, "ALTURA %d", (int)pl.p[1]);
        if (pl.won) rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 90, 110, "NIVEL COMPLETADO!  START=REINICIAR");
        rdpq_detach_show();
    }
    return 0;
}
