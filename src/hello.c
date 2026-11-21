/* ROM de prueba oficial-style: consola de texto de libdragon (sin framebuffer propio). */
#include <libdragon.h>
#include <stdio.h>
int main(void) {
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    int n = 0;
    while (1) {
        console_clear();
        printf("HOLA N64\nLa consola de texto funciona\n\nFrame %d\n", n++);
        console_render();
    }
    return 0;
}
