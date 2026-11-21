/* ROM de prueba: 32 bits, sin filtros, solo CPU. */
#include <libdragon.h>
#include <stdint.h>
int main(void) {
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    int n = 0;
    while (1) {
        surface_t *d = display_get();
        uint32_t col = (n / 60) % 3 == 0 ? 0xFF0000FF : ((n / 60) % 3 == 1 ? 0x00FF00FF : 0x0000FFFF);
        for (int y = 0; y < 240; y++) {
            uint32_t *r = (uint32_t *)((unsigned char *)d->buffer + y * d->stride);
            for (int x = 0; x < 320; x++) r[x] = col;
        }
        data_cache_hit_writeback_invalidate(d->buffer, d->stride * d->height);
        display_show(d);
        n++;
    }
    return 0;
}
