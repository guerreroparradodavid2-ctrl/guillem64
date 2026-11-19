/* ROM de prueba minima: solo pinta colores con la CPU y muestra texto. */
#include <libdragon.h>
#include <stdio.h>
int main(void) {
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    int n = 0;
    while (1) {
        surface_t *d = display_get();
        unsigned short col = (n / 60) % 3 == 0 ? 0xF801 : ((n / 60) % 3 == 1 ? 0x07C1 : 0x003F);
        for (int y = 0; y < 240; y++) {
            unsigned short *r = (unsigned short *)((unsigned char *)d->buffer + y * d->stride);
            for (int x = 0; x < 320; x++) r[x] = col;
        }
        graphics_set_color(graphics_make_color(255, 255, 255, 255), graphics_make_color(0, 0, 0, 255));
        char t[40]; sprintf(t, "TEST CPU OK  %d", n);
        graphics_draw_text(d, 90, 110, t);
        data_cache_hit_writeback_invalidate(d->buffer, d->stride * d->height);
        display_show(d);
        n++;
    }
    return 0;
}
