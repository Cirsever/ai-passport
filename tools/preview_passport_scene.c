/* Host export of firmware geometry. Build:
 * cc -std=c11 -Imain tools/preview_passport_scene.c main/passport_scene.c \
 *    -o /tmp/preview_passport_scene
 * /tmp/preview_passport_scene > build/passport-scene.ppm
 * Panels: idle, single scanning, three scanning, four settled/selected,
 * and a settled card using the transferred indexed companion.
 */
#include "passport_scene.h"

#include <stdio.h>
#include <string.h>

#define SCALE 3
#define WIDTH (PASSPORT_SCENE_WIDTH * 5 * SCALE)
#define HEIGHT (PASSPORT_SCENE_HEIGHT * SCALE)
static unsigned char pixels[HEIGHT][WIDTH][3];
static int offset;

static void rect(void *ctx, int x, int y, int w, int h, uint32_t color) {
    (void)ctx;
    for (int row = y * SCALE; row < (y + h) * SCALE; row++) {
        for (int col = (offset + x) * SCALE; col < (offset + x + w) * SCALE; col++) {
            if (row < 0 || row >= HEIGHT || col < 0 || col >= WIDTH) continue;
            pixels[row][col][0] = (unsigned char)(color >> 16);
            pixels[row][col][1] = (unsigned char)(color >> 8);
            pixels[row][col][2] = (unsigned char)color;
        }
    }
}

int main(void) {
    const size_t counts[] = {0, 1, 3, 4, 1};
    for (int panel = 0; panel < 5; panel++) {
        passport_service_snapshot_t snapshot = {0};
        passport_scene_t scene = {
            .count = counts[panel], .elapsed_ms = panel == 3 ? 1400 : 500,
            .phase_ms = 500,
        };
        snapshot.stack_count = scene.count;
        snapshot.stack_selected = panel == 3 ? 2 : 0;
        strcpy(snapshot.stack[0].role, "agent");
        strcpy(snapshot.stack[1].role, "project");
        strcpy(snapshot.stack[2].role, "skill");
        strcpy(snapshot.stack[3].role, "review");
        if (panel == 4) {
            uint8_t packed[544] = {0};
            packed[2] = 0xE0;
            packed[3] = 0x07;
            for (int row = 7; row < 27; row++) {
                for (int col = 6; col < 26; col++) {
                    int pixel = row * 32 + col;
                    if (pixel & 1) packed[32 + pixel / 2] |= 1;
                    else packed[32 + pixel / 2] = 0x10;
                }
            }
            passport_scene_set_companion(&scene, packed, 1);
        }
        offset = panel * PASSPORT_SCENE_WIDTH;
        passport_scene_draw(&scene, &snapshot, rect, NULL);
    }
    printf("P6\n%d %d\n255\n", WIDTH, HEIGHT);
    return fwrite(pixels, sizeof(pixels), 1, stdout) == 1 ? 0 : 1;
}
