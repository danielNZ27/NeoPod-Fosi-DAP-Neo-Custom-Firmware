#include "boot_image.h"

#include "hal/display.h"
#include "hal/storage.h"

static HalFile file;
static bool available;

void boot_image_close(void)
{
    if (available) hal_file_close(&file);
    available = false;
}

void boot_image_open(void)
{
    boot_image_close();
    if (!hal_file_open(&file, "/.echo/boot.rgb565")) return;
    available = true;
    if (file.size != (uint32_t)HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT * sizeof(Color)) {
        boot_image_close();
    }
}

bool boot_image_draw(Canvas *canvas, uint8_t alpha)
{
    if (!available) return false;
    /* Read directly into the existing render strip; no full-screen allocation. */
    Rect area = canvas->bounds;
    if (area.x != 0 || area.width != HAL_DISPLAY_WIDTH || area.y < 0 ||
        area.y + area.height > HAL_DISPLAY_HEIGHT) return false;
    uint32_t offset = (uint32_t)area.y * HAL_DISPLAY_WIDTH * sizeof(Color);
    uint32_t size = (uint32_t)area.height * HAL_DISPLAY_WIDTH * sizeof(Color);
    if (!hal_file_read(file, offset, canvas->pixels, size)) {
        canvas_clear(canvas, COLOR_RGB(0, 0, 0));
        boot_image_close();
        return false;
    }
    for (uint32_t index = 0; index < size / sizeof(Color); index++) {
        canvas->pixels[index] = color_blend(COLOR_RGB(0, 0, 0), canvas->pixels[index], alpha);
    }
    return true;
}
