/* Guillem the Frutem Adventure - port no oficial para Nintendo 64 (libdragon).
   Etapa 1: nivel con cajas, camara en primera persona, gemas, trampolines, muerte y meta. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <stdlib.h>
#include "game.h"

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
    float t = (b->p[1] - 15.f) / 105.f; if (t < 0) t = 0; if (t > 1) t = 1;
    float v = 0.55f + 0.25f * (float)((i * 37) % 5) / 4.f;
    *r = v * (0.7f + 0.3f * t); *g = v * 0.75f; *bl = v * (1.0f - 0.3f * t);
}

static void draw_world(void) {
    float eye[3] = { pl.p[0], pl.p[1] + EYE, pl.p[2] };
    float cp = cosf(pl.pitch), sp = sinf(pl.pitch);
    float fx = -sinf(pl.yaw) * cp, fy = sp, fz = -cosf(pl.yaw) * cp;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = 320.f / 240.f, nr = 0.3f, fr = 260.f;
    glFrustum(-nr * aspect, nr * aspect, -nr, nr, nr, fr);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye[0], eye[1], eye[2], eye[0] + fx, eye[1] + fy, eye[2] + fz, 0, 1, 0);

    for (int i = 0; i < NBOX; i++) {
        const LBox *b = &LBOXES[i];
        float dx = b->p[0] - eye[0], dy = b->p[1] - eye[1], dz = b->p[2] - eye[2];
        float big = b->h[0] > b->h[2] ? b->h[0] : b->h[2];
        float lim = 110.f + big;
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
    /* gemas: cubitos amarillos que giran */
    for (int i = 0; i < NGEM; i++) {
        if (pl.got[i]) continue;
        float dx = LGEMS[i][0] - eye[0], dy = LGEMS[i][1] - eye[1], dz = LGEMS[i][2] - eye[2];
        if (dx*dx + dy*dy + dz*dz > 90.f * 90.f) continue;
        glPushMatrix();
        glTranslatef(LGEMS[i][0], LGEMS[i][1], LGEMS[i][2]);
        glRotatef(pl.t * 120.f, 0, 1, 0);
        glRotatef(45.f, 1, 0, 1);
        glScalef(0.7f, 0.7f, 0.7f);
        draw_cube(1.0f, 0.95f, 0.2f);
        glPopMatrix();
    }
}

int main(void) {
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    zbuf = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
    rdpq_init();
    gl_init();
    joypad_init();
    rdpq_font_t *fnt = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, fnt);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
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
        draw_world();
        gl_context_end();

        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 12, 18, "GEMAS %d/%d", pl.gems, NGEM);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 12, 30, "ALTURA %d", (int)pl.p[1]);
        if (pl.won) rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 90, 110, "NIVEL COMPLETADO!  START=REINICIAR");
        rdpq_detach_show();
    }
    return 0;
}
