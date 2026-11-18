
#include <libdragon.h>

int main(void) {
    display_init(
        RESOLUTION_320x240,
        DEPTH_16_BPP,
        2,
        GAMMA_NONE,
        FILTERS_RESAMPLE
    );

    rdpq_init();

    while (1) {
        surface_t *disp = display_get();
        rdpq_attach(disp, NULL);

        rdpq_clear(RGBA32(0, 100, 255, 255));
        rdpq_text_printf(NULL, 1, 80, 110, "GUILLEM N64 - TEST OK");

        rdpq_detach_show();
    }

    return 0;
}
