#include "boot_image.h"
#include "check.h"
#include "fake_hal.h"
#include "hal/display.h"

static Color artwork[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Color pixels[HAL_DISPLAY_WIDTH * 2];

static void missing_or_wrong_sized_artwork_uses_the_text_fallback(void)
{
    boot_image_close();
    fake_hal_reset();
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HAL_DISPLAY_WIDTH, 2});
    boot_image_open();
    CHECK(!boot_image_draw(&canvas, 15));
    fake_hal_add_file("/.echo/boot.rgb565", (const uint8_t *)artwork, 2);
    boot_image_open();
    CHECK(!boot_image_draw(&canvas, 15));
}

static void artwork_streams_the_correct_rows_and_fades_from_black(void)
{
    boot_image_close();
    fake_hal_reset();
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < HAL_DISPLAY_WIDTH; x++) {
            artwork[y * HAL_DISPLAY_WIDTH + x] = (Color)(y * HAL_DISPLAY_WIDTH + x);
        }
    }
    fake_hal_add_file("/.echo/boot.rgb565", (const uint8_t *)artwork, sizeof artwork);
    boot_image_open();
    Canvas canvas = canvas_make(pixels, (Rect){0, HAL_DISPLAY_HEIGHT - 2, HAL_DISPLAY_WIDTH, 2});
    CHECK(boot_image_draw(&canvas, 15));
    CHECK_EQUAL(artwork[HAL_DISPLAY_WIDTH * (HAL_DISPLAY_HEIGHT - 2)], pixels[0]);
    CHECK_EQUAL(artwork[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT - 1], pixels[HAL_DISPLAY_WIDTH * 2 - 1]);
    CHECK(boot_image_draw(&canvas, 7));
    CHECK_EQUAL(color_blend(0, artwork[HAL_DISPLAY_WIDTH * (HAL_DISPLAY_HEIGHT - 2)], 7), pixels[0]);
    CHECK(boot_image_draw(&canvas, 0));
    CHECK_EQUAL(0, pixels[0]);
    boot_image_close();
    CHECK(!boot_image_draw(&canvas, 15));
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(missing_or_wrong_sized_artwork_uses_the_text_fallback),
        TEST_CASE(artwork_streams_the_correct_rows_and_fades_from_black),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
