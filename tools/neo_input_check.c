#include <assert.h>
#include <limits.h>
#include <SDL.h>
#include "hal/input.h"
#include "sim_input.h"

static void wheel(int amount, uint32_t direction, Key expected, int pairs)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.y = amount;
    event.wheel.direction = direction;
    event.wheel.timestamp = 1234;
    sim_input_handle_event(&event);
    HalKeyTransition transition;
    for (int index = 0; index < pairs; index++) {
        assert(hal_input_next_transition(&transition));
        assert(transition.key == expected && transition.pressed && transition.time_ms == 1234);
        assert(hal_input_next_transition(&transition));
        assert(transition.key == expected && !transition.pressed);
    }
    assert(!hal_input_next_transition(&transition));
}

int main(void)
{
    wheel(2, SDL_MOUSEWHEEL_NORMAL, KEY_WHEEL_UP, 2);
    wheel(-1, SDL_MOUSEWHEEL_NORMAL, KEY_WHEEL_DOWN, 1);
    wheel(1, SDL_MOUSEWHEEL_FLIPPED, KEY_WHEEL_DOWN, 1);
    wheel(0, SDL_MOUSEWHEEL_NORMAL, KEY_WHEEL_UP, 0);
    wheel(INT_MIN, SDL_MOUSEWHEEL_NORMAL, KEY_WHEEL_DOWN, 16);
    return 0;
}
