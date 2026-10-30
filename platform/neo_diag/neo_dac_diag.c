/*
 * Fosi DAP Neo V145 standalone DAC bus diagnostic.
 * Adapted from the proven panel/input diagnostic below.
 *
 * A single-file, self-contained core 0 program packed with EchoPod's
 * tools/make_image.py container. It does NOT touch storage, audio, USB or
 * the update path. It only:
 *   - holds power (GPIO2.6) and releases it on a 2 s Power hold;
 *   - runs the core from the 24 MHz crystal;
 *   - initialises the panel with the init sequence recovered from stock
 *     DP3_V137 module 4 (ST7789-class, 240x320 portrait, MADCTL 0x00);
 *   - shows live SARADC channels 0-5, wheel pins (GPIO2.12/2.13), power,
 *     charge, jack, card-detect and VBUS levels, plus a quadrature count.
 *
 * Pin evidence (stock DP3_V137, see investigation/gpio-calls-all.txt and
 * investigation/lcd-mod04.txt):
 *   panel data GPIO0.16-23, WR GPIO0.24, DC GPIO0.25, RD GPIO0.12,
 *   CS GPIO0.15, reset GPIO2.15, backlight GPIO2.4, power key GPIO2.5,
 *   power hold GPIO2.6, charge GPIO2.3, wheel GPIO2.12/2.13.
 */
#include <stdbool.h>
#include <stdint.h>

#define GPIO0_BASE 0x40160000u
#define GPIO1_BASE 0x40170000u
#define GPIO2_BASE 0x50030000u
#define GPIO_DATA 0x00u
#define GPIO_DIRECTION 0x04u
#define GPIO_INPUT_LEVEL 0x50u
#define GRF_BASE 0x50010000u
#define GRF_PORT2_LOW_PULL (GRF_BASE + 0x50u)
#define GRF_VBUS (GRF_BASE + 0xE8u)
#define SARADC_BASE 0x400D0000u
#define ADC_RESULT (SARADC_BASE + 0x00u)
#define ADC_CONTROL (SARADC_BASE + 0x08u)
#define CRU_BASE 0x40180000u
#define CRU_MODE (CRU_BASE + 0x10u)
#define CRU_CLOCK_SELECT (CRU_BASE + 0x14u)
#define CRU_PCLK_SELECT (CRU_BASE + 0x18u)
#define SYSTICK_CTRL 0xE000E010u
#define SYSTICK_RELOAD 0xE000E014u
#define SYSTICK_CURRENT 0xE000E018u
#define NVIC_ICER0 0xE000E180u
#define NVIC_ICER1 0xE000E184u
#define NVIC_ICPR0 0xE000E280u
#define NVIC_ICPR1 0xE000E284u
#define SCB_VTOR 0xE000ED08u
#define SCB_AIRCR 0xE000ED0Cu
#define SCB_SHCSR 0xE000ED24u

#define STACK_TOP 0x03004000u
#define CODE_VECTORS_ADDRESS 0x03050000u

#define PANEL_DATA_SHIFT 16u
#define PANEL_WR (1u << 24)
#define PANEL_DC (1u << 25)
#define PANEL_RD (1u << 12)
#define PANEL_DIRECTION_BITS 0x03FF9000u /* RD 12, CS 15, data 16-23, WR 24, DC 25 */

enum {
    WIDTH = 240,
    HEIGHT = 320,
    TICKS_PER_MS = 24000,
    ADC_CHANNELS = 6,
    POWER_OFF_HOLD_MS = 2000,
    IDLE_OFF_MS = 10 * 60 * 1000,
    KEY_DELTA = 40,
    HISTORY = 4
};

static inline uint32_t reg_read(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}

static inline void reg_write(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

/* ---------------------------------------------------------------- GPIO */

typedef struct Pin {
    uint8_t port;
    uint8_t pin;
} Pin;

static const Pin BACKLIGHT = {2, 4};
static const Pin POWER_KEY = {2, 5};
static const Pin POWER_HOLD = {2, 6};
static const Pin CHARGE = {2, 3};
static const Pin PANEL_RESET = {2, 15};
static const Pin WHEEL_A = {2, 12};
static const Pin WHEEL_B = {2, 13};
static const Pin JACK = {0, 13};
static const Pin CARD = {0, 11};
static const Pin P2_14 = {2, 14};

static uint32_t port_base(uint8_t port)
{
    return port == 0 ? GPIO0_BASE : port == 1 ? GPIO1_BASE : GPIO2_BASE;
}

static void pin_mux_gpio(Pin pin)
{
    uint32_t address = GRF_BASE + 0x20u * pin.port + 4u * (uint32_t)(pin.pin / 8u);
    uint32_t shift = 2u * (uint32_t)(pin.pin % 8u);
    reg_write(address, (0u | (3u << 16)) << shift);
}

static void set_bit(uint32_t address, uint8_t bit, bool value)
{
    uint32_t current = reg_read(address);
    reg_write(address, value ? current | (1u << bit) : current & ~(1u << bit));
}

static void pin_output(Pin pin, bool level)
{
    uint32_t base = port_base(pin.port);
    set_bit(base + GPIO_DATA, pin.pin, level);
    set_bit(base + GPIO_DIRECTION, pin.pin, true);
}

static void pin_input(Pin pin)
{
    set_bit(port_base(pin.port) + GPIO_DIRECTION, pin.pin, false);
}

static bool pin_read(Pin pin)
{
    return (reg_read(port_base(pin.port) + GPIO_INPUT_LEVEL) >> pin.pin & 1u) != 0;
}

/* --------------------------------------------------------------- clock */

static uint32_t last_tick;
static uint32_t elapsed_ticks;
static uint32_t now_ms_value;

static void clock_start(void)
{
    /* Same register writes as EchoPod's leave_pll(): core from the crystal. */
    reg_write(CRU_MODE, 0x00010000u);
    reg_write(CRU_CLOCK_SELECT, 0xFFFF0000u);
    reg_write(CRU_PCLK_SELECT, 0x03000000u);
    reg_write(SYSTICK_CTRL, 0);
    reg_write(SYSTICK_RELOAD, 0x00FFFFFFu);
    reg_write(SYSTICK_CURRENT, 0);
    reg_write(SYSTICK_CTRL, 0x5u); /* core clock, enabled, no interrupt */
    last_tick = reg_read(SYSTICK_CURRENT);
}

static uint32_t now_ms(void)
{
    uint32_t tick = reg_read(SYSTICK_CURRENT);
    elapsed_ticks += (last_tick - tick) & 0x00FFFFFFu; /* counts down */
    last_tick = tick;
    while (elapsed_ticks >= TICKS_PER_MS) {
        elapsed_ticks -= TICKS_PER_MS;
        now_ms_value++;
    }
    return now_ms_value;
}

static void poll_inputs(void);

static void delay_ms(uint32_t ms)
{
    uint32_t start = now_ms();
    while (now_ms() - start < ms) {
        poll_inputs();
    }
}

static void delay_short(void)
{
    for (volatile int i = 0; i < 30; i++) {
    }
}

/* --------------------------------------------------------------- panel */

static uint32_t port_shadow;

static void bus_write(uint32_t word)
{
    volatile uint32_t *port = (volatile uint32_t *)(uintptr_t)(GPIO0_BASE + GPIO_DATA);
    *port = word;
    *port = word;
    *port = word | PANEL_WR;
}

static void send_byte(uint8_t value, bool data)
{
    uint32_t bits = (0xFFu << PANEL_DATA_SHIFT) | PANEL_WR | PANEL_DC;
    uint32_t word = (port_shadow & ~bits) | ((uint32_t)value << PANEL_DATA_SHIFT);
    bus_write(data ? word | PANEL_DC : word);
}

static void lcd_cmd(uint8_t command)
{
    send_byte(command, false);
}

static void lcd_data(uint8_t value)
{
    send_byte(value, true);
}

/* cmd, count, data..., 0xFF terminates. Recovered from stock module 4
 * (0x0307ce68..0x0307d05e). */
static const uint8_t NEO_INIT[] = {
    0x36, 1, 0x00,
    0x3A, 1, 0x05,
    0x21, 0,
    0xB2, 5, 0x05, 0x05, 0x00, 0x33, 0x33,
    0xB7, 1, 0x64,
    0xBB, 1, 0x2A,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x13,
    0xC4, 1, 0x20,
    0xC6, 1, 0x11,
    0xD0, 2, 0xA4, 0xA1,
    0xD6, 1, 0xA1,
    0xE0, 14, 0xD0, 0x06, 0x06, 0x0E, 0x0D, 0x06, 0x2F, 0x3A, 0x47, 0x08, 0x15, 0x14, 0x2C, 0x33,
    0xE1, 14, 0xD0, 0x06, 0x06, 0x0E, 0x0D, 0x06, 0x2F, 0x3B, 0x47, 0x08, 0x15, 0x14, 0x2C, 0x33,
    0x29, 0,
    0xFF,
};

static void panel_reset_pulse(uint32_t low_ms, uint32_t settle_ms)
{
    pin_output(PANEL_RESET, true);
    delay_ms(50);
    pin_output(PANEL_RESET, false);
    delay_ms(low_ms);
    pin_output(PANEL_RESET, true);
    delay_ms(settle_ms);
}

static void panel_start(void)
{
    for (uint8_t pin = 16; pin <= 25; pin++) {
        pin_mux_gpio((Pin){0, pin});
    }
    pin_mux_gpio((Pin){0, 15});
    pin_mux_gpio((Pin){0, 12});
    pin_mux_gpio(PANEL_RESET);

    uint32_t direction = reg_read(GPIO0_BASE + GPIO_DIRECTION);
    uint32_t data = reg_read(GPIO0_BASE + GPIO_DATA);
    port_shadow = (data & ~PANEL_DIRECTION_BITS) | PANEL_RD; /* CS low, RD high */
    reg_write(GPIO0_BASE + GPIO_DATA, port_shadow | PANEL_WR);
    reg_write(GPIO0_BASE + GPIO_DIRECTION, direction | PANEL_DIRECTION_BITS);

    panel_reset_pulse(150, 30);
    panel_reset_pulse(150, 120);
    delay_ms(20);
    lcd_cmd(0x11);
    delay_ms(120);
    for (const uint8_t *step = NEO_INIT; *step != 0xFF; step += 2u + step[1]) {
        lcd_cmd(step[0]);
        for (uint8_t i = 0; i < step[1]; i++) {
            lcd_data(step[2 + i]);
        }
    }
    delay_ms(10);
}

static void panel_window(int x, int y, int w, int h)
{
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    lcd_cmd(0x2A);
    lcd_data((uint8_t)(x >> 8));
    lcd_data((uint8_t)x);
    lcd_data((uint8_t)(x1 >> 8));
    lcd_data((uint8_t)x1);
    lcd_cmd(0x2B);
    lcd_data((uint8_t)(y >> 8));
    lcd_data((uint8_t)y);
    lcd_data((uint8_t)(y1 >> 8));
    lcd_data((uint8_t)y1);
    lcd_cmd(0x2C);
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    panel_window(x, y, w, h);
    uint8_t high = (uint8_t)(color >> 8);
    uint8_t low = (uint8_t)color;
    uint32_t count = (uint32_t)w * (uint32_t)h;
    for (uint32_t i = 0; i < count; i++) {
        lcd_data(high);
        lcd_data(low);
        if ((i & 1023u) == 0) {
            poll_inputs();
        }
    }
}

/* ---------------------------------------------------------------- font */

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define BLACK RGB(0, 0, 0)
#define WHITE RGB(255, 255, 255)
#define RED RGB(255, 0, 0)
#define GREEN RGB(0, 255, 0)
#define BLUE RGB(0, 0, 255)
#define YELLOW RGB(255, 255, 0)
#define CYAN RGB(0, 255, 255)
#define GREY RGB(128, 128, 128)
#define ORANGE RGB(255, 140, 0)

typedef struct Glyph {
    char c;
    uint8_t rows[7];
} Glyph;

static const Glyph FONT[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
};

static const uint8_t *glyph_rows(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (unsigned i = 0; i < sizeof FONT / sizeof FONT[0]; i++) {
        if (FONT[i].c == c) {
            return FONT[i].rows;
        }
    }
    return FONT[0].rows;
}

static void draw_char(int x, int y, char c, int scale, uint16_t fg, uint16_t bg)
{
    const uint8_t *rows = glyph_rows(c);
    int w = 6 * scale;
    int h = 8 * scale;
    panel_window(x, y, w, h);
    for (int py = 0; py < h; py++) {
        int row = py / scale;
        uint8_t bits = row < 7 ? rows[row] : 0;
        for (int px = 0; px < w; px++) {
            int col = px / scale;
            bool on = col < 5 && (bits >> (4 - col) & 1u);
            uint16_t color = on ? fg : bg;
            lcd_data((uint8_t)(color >> 8));
            lcd_data((uint8_t)color);
        }
    }
    poll_inputs();
}

static void draw_text(int col, int row, const char *text, uint16_t fg)
{
    int x = col * 12;
    int y = row * 16;
    for (; *text && x <= WIDTH - 12; text++, x += 12) {
        draw_char(x, y, *text, 2, fg, BLACK);
    }
}

static char *put_uint(char *out, uint32_t value, int width)
{
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < 10);
    while (n < width) {
        digits[n++] = ' ';
    }
    while (n) {
        *out++ = digits[--n];
    }
    *out = '\0';
    return out;
}

static char *put_text(char *out, const char *text)
{
    while (*text) {
        *out++ = *text++;
    }
    *out = '\0';
    return out;
}

static char *put_int(char *out, int32_t value, int width)
{
    char sign = value < 0 ? '-' : '+';
    uint32_t magnitude = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    *out++ = sign;
    return put_uint(out, magnitude, width - 1);
}

/* --------------------------------------------------------------- input */

static uint16_t adc_value[ADC_CHANNELS];
static uint16_t adc_baseline[ADC_CHANNELS];
static uint8_t adc_channel;
static uint32_t adc_started_ms;
static bool adc_running;

static uint8_t wheel_state;
static int32_t wheel_steps;
static int8_t wheel_last;
static uint32_t wheel_errors;

static uint32_t last_activity_ms;

typedef struct KeyEvent {
    uint8_t channel;
    uint16_t value;
} KeyEvent;

static KeyEvent history[HISTORY];
static uint8_t history_count;
static bool key_down[ADC_CHANNELS];
static uint16_t key_extreme[ADC_CHANNELS];

static void adc_start(uint8_t channel)
{
    uint32_t control = reg_read(ADC_CONTROL) & ~(1u << 4);
    reg_write(ADC_CONTROL, control);
    control &= ~(0x7u | (1u << 6));
    reg_write(ADC_CONTROL, control | channel | (1u << 3) | (1u << 4));
    adc_started_ms = now_ms();
    adc_running = true;
}

static void record_key(uint8_t channel, uint16_t value)
{
    for (int i = HISTORY - 1; i > 0; i--) {
        history[i] = history[i - 1];
    }
    history[0] = (KeyEvent){channel, value};
    if (history_count < HISTORY) {
        history_count++;
    }
}

static void adc_poll(void)
{
    if (!adc_running) {
        adc_start(0);
        return;
    }
    uint32_t now = now_ms();
    if (now - adc_started_ms < 2) {
        return;
    }
    uint16_t value = (uint16_t)(reg_read(ADC_RESULT) & 0x3FFu);
    uint8_t ch = adc_channel;
    adc_value[ch] = value;
    int delta = (int)value - (int)adc_baseline[ch];
    int magnitude = delta < 0 ? -delta : delta;
    if (adc_baseline[ch] != 0 && ch != 0) {
        if (magnitude > KEY_DELTA) {
            if (!key_down[ch]) {
                key_down[ch] = true;
                key_extreme[ch] = value;
            } else {
                int old = (int)key_extreme[ch] - (int)adc_baseline[ch];
                if ((old < 0 ? -old : old) < magnitude) {
                    key_extreme[ch] = value;
                }
            }
            last_activity_ms = now;
        } else if (key_down[ch]) {
            key_down[ch] = false;
            record_key(ch, key_extreme[ch]);
        }
    }
    adc_channel = (uint8_t)((adc_channel + 1u) % ADC_CHANNELS);
    adc_start(adc_channel);
}

static void wheel_poll(void)
{
    /* Gray-code transition table: index = old << 2 | new. */
    static const int8_t STEP[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
    uint8_t now = (uint8_t)((pin_read(WHEEL_A) ? 2u : 0u) | (pin_read(WHEEL_B) ? 1u : 0u));
    if (now == wheel_state) {
        return;
    }
    uint8_t index = (uint8_t)(wheel_state << 2 | now);
    int8_t step = STEP[index];
    if (step == 0) {
        wheel_errors++;
    } else {
        wheel_steps += step;
        wheel_last = step;
    }
    wheel_state = now;
    last_activity_ms = now_ms();
}



static void poll_inputs(void) {}

static void inputs_start(void)
{
    pin_mux_gpio(WHEEL_A);
    pin_mux_gpio(WHEEL_B);
    pin_input(WHEEL_A);
    pin_input(WHEEL_B);
    pin_input(CHARGE);
    pin_input(JACK);
    pin_input(CARD);
    wheel_state = (uint8_t)((pin_read(WHEEL_A) ? 2u : 0u) | (pin_read(WHEEL_B) ? 1u : 0u));
    adc_start(0);
    /* Two full sweeps, then use the result as the idle baseline. */
    uint32_t start = now_ms();
    while (now_ms() - start < 40) {
        poll_inputs();
    }
    for (int i = 0; i < ADC_CHANNELS; i++) {
        adc_baseline[i] = adc_value[i] ? adc_value[i] : 1;
    }
}

/* --------------------------------------------------------------- power */

static void power_hold(void)
{
    pin_mux_gpio(POWER_HOLD);
    pin_output(POWER_HOLD, true);
    pin_mux_gpio(POWER_KEY);
    pin_input(POWER_KEY);
    reg_write(GRF_PORT2_LOW_PULL, 0x00200000u); /* EchoPod: power key pull off */
}

static bool vbus_present(void)
{
    return (reg_read(GRF_VBUS) & (1u << 1)) != 0;
}

static void power_off(const char *reason)
{
    fill_rect(0, 0, WIDTH, HEIGHT, BLACK);
    draw_text(2, 8, "POWER OFF", ORANGE);
    draw_text(2, 10, reason, WHITE);
    delay_ms(800);
    pin_output(BACKLIGHT, false);
    pin_output(POWER_HOLD, false);
    for (;;) {
        /* Still running: USB power keeps the player on. */
        delay_ms(500);
        if (!vbus_present()) {
            pin_output(POWER_HOLD, false);
        }
    }
}

/* ---------------------------------------------------------------- main */

static void draw_static(void)
{
    fill_rect(0, 0, WIDTH, HEIGHT, BLACK);
    /* Corner markers: TL red, TR green, BL blue, BR white. */
    fill_rect(0, 0, 8, 8, RED);
    fill_rect(WIDTH - 8, 0, 8, 8, GREEN);
    fill_rect(0, HEIGHT - 8, 8, 8, BLUE);
    fill_rect(WIDTH - 8, HEIGHT - 8, 8, 8, WHITE);
    draw_text(1, 0, "DAP NEO DIAG 1", YELLOW);
    /* Colour bars: red, green, blue, white, grey. */
    static const uint16_t BARS[] = {RED, GREEN, BLUE, WHITE, GREY};
    for (int i = 0; i < 5; i++) {
        fill_rect(i * 48, 18, 48, 12, BARS[i]);
    }
    draw_text(0, 2, " R   G   B   W   GR", GREY);
    draw_text(0, 18, "HOLD POWER 2S: OFF", GREY);
}

static void draw_live(void)
{
    char line[24];
    char *p;
    for (int pair = 0; pair < 3; pair++) {
        p = line;
        for (int k = 0; k < 2; k++) {
            int ch = pair * 2 + k;
            p = put_text(p, "A");
            p = put_uint(p, (uint32_t)ch, 1);
            p = put_text(p, ":");
            p = put_uint(p, adc_value[ch], 4);
            p = put_text(p, k == 0 ? "  " : "");
        }
        draw_text(0, 4 + pair, line, key_down[pair * 2] || key_down[pair * 2 + 1] ? GREEN : WHITE);
    }

    p = line;
    p = put_text(p, "WHEEL A");
    p = put_uint(p, pin_read(WHEEL_A), 1);
    p = put_text(p, " B");
    p = put_uint(p, pin_read(WHEEL_B), 1);
    p = put_text(p, " E");
    put_uint(p, wheel_errors % 1000u, 3);
    draw_text(0, 8, line, CYAN);

    p = line;
    p = put_text(p, "STEPS");
    p = put_int(p, wheel_steps, 6);
    p = put_text(p, wheel_last > 0 ? " +" : wheel_last < 0 ? " -" : "  ");
    draw_text(0, 9, line, CYAN);

    p = line;
    p = put_text(p, "PWR");
    p = put_uint(p, pin_read(POWER_KEY), 1);
    p = put_text(p, " CHG");
    p = put_uint(p, pin_read(CHARGE), 1);
    p = put_text(p, " USB");
    put_uint(p, vbus_present(), 1);
    draw_text(0, 11, line, WHITE);

    p = line;
    p = put_text(p, "JACK");
    p = put_uint(p, pin_read(JACK), 1);
    p = put_text(p, " SD");
    p = put_uint(p, pin_read(CARD), 1);
    p = put_text(p, " P214:");
    put_uint(p, pin_read(P2_14), 1);
    draw_text(0, 12, line, WHITE);

    draw_text(0, 13, "KEYS (CH=PEAK):", GREY);
    for (int i = 0; i < HISTORY; i++) {
        p = line;
        if (i < history_count) {
            p = put_text(p, "  A");
            p = put_uint(p, history[i].channel, 1);
            p = put_text(p, "=");
            p = put_uint(p, history[i].value, 4);
            p = put_text(p, "     ");
        } else {
            p = put_text(p, "              ");
        }
        draw_text(0, 14 + i, line, i == 0 ? GREEN : WHITE);
    }

    p = line;
    p = put_text(p, "UP");
    p = put_uint(p, now_ms() / 1000u, 6);
    p = put_text(p, "S");
    draw_text(0, 19, line, GREY);
}

void legacy_main(void)
{
    power_hold();
    clock_start();
    pin_mux_gpio(BACKLIGHT);
    pin_output(BACKLIGHT, false);
    panel_start();
    inputs_start();
    draw_static();
    /* Backlight polarity check: lit 1 s, dark 1 s, three times, then lit. */
    for (int i = 0; i < 3; i++) {
        pin_output(BACKLIGHT, true);
        delay_ms(1000);
        pin_output(BACKLIGHT, false);
        delay_ms(1000);
    }
    pin_output(BACKLIGHT, true);
    last_activity_ms = now_ms();

    uint32_t power_down_since = 0;
    bool power_was_down = pin_read(POWER_KEY); /* ignore the boot press */
    for (;;) {
        draw_live();
        bool power_down = pin_read(POWER_KEY);
        uint32_t now = now_ms();
        if (power_down && !power_was_down) {
            power_down_since = now;
            last_activity_ms = now;
        }
        if (!power_down) {
            power_was_down = false;
        } else if (!power_was_down && now - power_down_since >= POWER_OFF_HOLD_MS) {
            power_off("POWER KEY");
        }
        if (power_down && power_was_down) {
            /* Boot press still held: wait for release. */
        }
        if (!vbus_present() && now - last_activity_ms > IDLE_OFF_MS) {
            power_off("IDLE 10 MIN");
        }
    }
}


/* V145: address-only scans, then a read-only CS43198 ID transaction.
 * Software I2C releases HIGH and drives only LOW on verified group A pins. */
static const Pin SCL={2,10}, SDA={2,11};
static uint32_t boot_regs[7], hw_pending;
static uint8_t hw_ack[112], sw_ack[112], hw_count, sw_count, sw_id;
static uint8_t hw_idle, sw_idle;
static bool sw_fault, id_ok;
static void gate_on(unsigned id) {
    reg_write(CRU_BASE+0x80+4*(id>>4), 1u<<((id&15)+16));
}
static void pace(void) { for(volatile unsigned n=0;n<90;n++){} (void)now_ms(); }
static void release(Pin p) { pin_input(p); pace(); }
static void low(Pin p) { pin_output(p,false); pace(); }
static bool rise(void) {
    release(SCL); uint32_t t=now_ms();
    while(!pin_read(SCL)) if(now_ms()-t>=2) { sw_fault=true; return false; }
    return true;
}
static bool sw_start(void) {
    release(SDA); if(!rise() || !pin_read(SDA)) { sw_fault=true; return false; }
    low(SDA); low(SCL); return true;
}
static void sw_stop(void) { low(SCL); low(SDA); rise(); release(SDA); }
static bool sw_byte(uint8_t b) {
    for(unsigned i=0;i<8;i++) {
        if(b&128) release(SDA); else low(SDA);
        if(!rise()) return false;
        low(SCL); b<<=1;
    }
    release(SDA); if(!rise()) return false;
    bool ack=!pin_read(SDA); low(SCL); return ack;
}
static uint8_t sw_read(void) {
    uint8_t b=0; release(SDA);
    for(unsigned i=0;i<8;i++) { if(!rise()) return 255; b=(b<<1)|pin_read(SDA); low(SCL); }
    release(SDA); rise(); low(SCL); return b; /* final NACK */
}
#define IB 0x40040000u
static uint32_t pending(unsigned mask) {
    uint32_t t=now_ms(),v;
    do { v=reg_read(IB+0x1c); if(v&mask) return v; } while(now_ms()-t<10);
    return v;
}
static bool hw_probe(uint8_t addr) {
    reg_write(IB+0x1c,127); reg_write(IB,9);
    uint32_t v=pending(16); bool ack=false;
    if(v&16) {
        reg_write(IB+0x1c,16); reg_write(IB,1);
        reg_write(IB+0x100,(unsigned)addr<<1);
        reg_write(IB+0x1c,127); reg_write(IB+0x10,1);
        v=pending(4|64); ack=(v&4) && !(v&64);
    }
    if(addr==0x33) hw_pending=v;
    reg_write(IB+0x1c,127); reg_write(IB,17); pending(32);
    reg_write(IB,0); reg_write(IB+0x1c,127); return ack;
}
static void run_probes(void) {
    reg_write(GRF_BASE+0xbc,0x30000);
    reg_write(GRF_BASE+0x44,0x00f00050); /* B2 SCL, B3 SDA, function 1 */
    gate_on(0x83);
    reg_write(CRU_BASE+0xc4,0x04000400); pace();
    reg_write(CRU_BASE+0xc4,0x04000000);
    reg_write(IB,0); reg_write(IB+4,0x000e000e); reg_write(IB+0x18,0);
    hw_idle=(pin_read(SCL)?2:0)|(pin_read(SDA)?1:0);
    for(unsigned a=8;a<120;a++) if(hw_probe(a)) hw_ack[hw_count++]=a;
    reg_write(IB,0); pin_mux_gpio(SCL); pin_mux_gpio(SDA);
    release(SCL); release(SDA);
    sw_idle=(pin_read(SCL)?2:0)|(pin_read(SDA)?1:0);
    for(unsigned a=8;a<120;a++) {
        bool ack=sw_start() && sw_byte(a<<1); sw_stop();
        if(ack) sw_ack[sw_count++]=a;
    }
    bool ok=sw_start();
    if(ok) ok=sw_byte(0x66);
    if(ok) ok=sw_byte(1);
    if(ok) ok=sw_byte(0);
    if(ok) ok=sw_byte(0);
    if(ok) ok=sw_byte(1);
    sw_stop();
    if(ok) { ok=sw_start() && sw_byte(0x67); if(ok) sw_id=sw_read(); sw_stop(); }
    id_ok=ok && !sw_fault;
}
static void hex_line(unsigned row,const char*label,uint32_t v) {
    char line[32]; char*p=line;
    for(unsigned n=0;label[n] && n<12;n++) *p++=label[n];
    for(int i=7;i>=0;i--) *p++="0123456789ABCDEF"[(v>>(4*i))&15];
    *p=0; draw_text(0,row,line,WHITE);
}
static void ack_lines(unsigned row,const char*label,uint8_t*list,unsigned count,unsigned offset) {
    draw_text(0,row,label,GREEN);
    for(unsigned j=0;j<4;j++) {
        char line[21]; char*p=line;
        for(unsigned k=0;k<6 && offset+j*6+k<count;k++) {
            unsigned a=list[offset+j*6+k];
            *p++="0123456789ABCDEF"[a>>4]; *p++="0123456789ABCDEF"[a&15]; *p++=' ';
        }
        *p=0; draw_text(0,row+1+j,line,WHITE);
    }
}
static void show_page(unsigned page) {
    fill_rect(0,0,WIDTH,HEIGHT,BLACK);
    draw_text(0,0,"NEO DAC DIAG V145",GREEN);
    if(page==0) {
        draw_text(0,2,"PAGE 1: SUMMARY",WHITE);
        hex_line(4,"HW ACK COUNT ",hw_count);
        hex_line(5,"HW 33 IPD   ",hw_pending);
        hex_line(6,"HW BUS IDLE ",hw_idle);
        hex_line(8,"SW ACK COUNT ",sw_count);
        hex_line(9,"SW BUS IDLE  ",sw_idle);
        hex_line(10,"SW BUS FAULT ",sw_fault);
        hex_line(12,"SW ID READ OK",id_ok);
        hex_line(13,"SW DEVICE ID ",sw_id);
        draw_text(0,15,"IDLE: 3 = BOTH HIGH",GREY);
        draw_text(0,16,"ADDRS ARE 7-BIT HEX",GREY);
    } else if(page==1) {
        draw_text(0,2,"PAGE 2: BOOT REGS",WHITE);
        const char*names[]={"G0 OUT ","G0 DIR ","G1 OUT ","G1 DIR ","G2 OUT ","G2 DIR ","G2 IN  "};
        for(unsigned i=0;i<7;i++) hex_line(4+i,names[i],boot_regs[i]);
        hex_line(12,"G1 NOW ",reg_read(GPIO1_BASE));
        hex_line(13,"G2 NOW ",reg_read(GPIO2_BASE));
        hex_line(14,"G2 DIR ",reg_read(GPIO2_BASE+4));
        hex_line(15,"G2 IN  ",reg_read(GPIO2_BASE+0x50));
    } else {
        char title[]="PAGE 3: ACK ADDRESSES"; title[5]='3'+page-2;
        draw_text(0,2,title,WHITE);
        ack_lines(4,"HARDWARE:",hw_ack,hw_count,(page-2)*24);
        ack_lines(10,"SOFTWARE:",sw_ack,sw_count,(page-2)*24);
    }
    draw_text(0,18,"OK: NEXT PAGE",GREEN);
    draw_text(0,19,"HOLD POWER: OFF",GREY);
}
void neo_main(void) {
    sw_id=255;
    boot_regs[0]=reg_read(GPIO0_BASE); boot_regs[1]=reg_read(GPIO0_BASE+4);
    boot_regs[2]=reg_read(GPIO1_BASE); boot_regs[3]=reg_read(GPIO1_BASE+4);
    boot_regs[4]=reg_read(GPIO2_BASE); boot_regs[5]=reg_read(GPIO2_BASE+4);
    boot_regs[6]=reg_read(GPIO2_BASE+0x50);
    power_hold(); clock_start(); gate_on(0x95);
    const Pin pins[]={{0,14},{2,0},{2,8},{1,4}};
    for(unsigned i=0;i<4;i++) { pin_mux_gpio(pins[i]); pin_output(pins[i],true); }
    pin_mux_gpio(BACKLIGHT); pin_output(BACKLIGHT,false);
    panel_start(); pin_mux_gpio(P2_14); pin_input(P2_14);
    pin_output(BACKLIGHT,true);
    fill_rect(0,0,WIDTH,HEIGHT,BLACK); draw_text(0,3,"SCANNING I2C...",GREEN);
    /* Do not run ADC polling: it is unrelated to this test. */
    uint32_t start=now_ms(); while(now_ms()-start<50) {}
    run_probes();
    unsigned pages=3; unsigned max=hw_count>sw_count?hw_count:sw_count;
    if(max>24) pages=2+(max+23)/24;
    unsigned page=0; show_page(page);
    bool ok_was=!pin_read(P2_14), power_boot=pin_read(POWER_KEY);
    uint32_t down=0,last_ok=0;
    for(;;) {
        uint32_t now=now_ms(); bool ok=!pin_read(P2_14);
        if(ok&&!ok_was && now-last_ok>200) { page=(page+1)%pages; show_page(page); last_ok=now; }
        ok_was=ok;
        if(!pin_read(POWER_KEY)) { power_boot=false; down=0; }
        else if(!power_boot) {
            if(!down) down=now;
            if(now-down>=2000) { pin_output(BACKLIGHT,false); pin_output(POWER_HOLD,false); }
        }
    }
}

/* ------------------------------------------------------------- startup */

extern uint32_t device_bss_start;
extern uint32_t device_bss_end;

void device_reset(void);
void device_fault(void);

typedef void (*Handler)(void);

__attribute__((section(".entry_vector"), used)) static const Handler ENTRY_VECTOR[2] = {
    (Handler)STACK_TOP,
    device_reset,
};

__attribute__((section(".code_vectors"), used, aligned(256))) static const Handler CODE_VECTORS[16] = {
    (Handler)STACK_TOP, device_reset, device_fault, device_fault, device_fault, device_fault,
    device_fault,       0,            0,            0,            0,            device_fault,
    device_fault,       0,            device_fault, device_fault,
};

void device_fault(void)
{
    /* Show something visible, then let the power key switch off. */
    pin_output(BACKLIGHT, true);
    for (;;) {
        if (pin_read(POWER_KEY)) {
            pin_output(POWER_HOLD, false);
        }
    }
}

void device_reset(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    reg_write(SYSTICK_CTRL, 0);
    reg_write(NVIC_ICER0, 0xFFFFFFFFu);
    reg_write(NVIC_ICER1, 0xFFFFFFFFu);
    reg_write(NVIC_ICPR0, 0xFFFFFFFFu);
    reg_write(NVIC_ICPR1, 0xFFFFFFFFu);
    reg_write(SCB_SHCSR, 0);
    reg_write(SCB_AIRCR, 0x05FA0002u);
    for (volatile uint32_t *word = &device_bss_start; word < &device_bss_end; word++) {
        *word = 0;
    }
    reg_write(SCB_VTOR, CODE_VECTORS_ADDRESS);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    neo_main();
    for (;;) {
    }
}
