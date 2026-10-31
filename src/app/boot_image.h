#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx/canvas.h"

void boot_image_open(void);
void boot_image_close(void);
bool boot_image_draw(Canvas *canvas, uint8_t alpha);
