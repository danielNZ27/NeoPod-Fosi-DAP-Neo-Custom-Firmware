#include "check.h"
#include "device/pcm_transition.h"

static void fades_preserve_polarity_and_end_at_silence(void)
{
    uint32_t ring[32];
    for (int i = 0; i < 16; ++i) { ring[2*i] = 0x7FFFFF00u; ring[2*i+1] = 0x80000000u; }
    pcm_transition(ring, 16, 0, 16, false);
    CHECK_EQUAL(0x7FFFFF00u, ring[0]);
    CHECK_EQUAL(0x80000000u, ring[1]);
    for (int i = 1; i < 16; ++i) {
        CHECK((int32_t)ring[2*i] <= (int32_t)ring[2*(i-1)]);
        CHECK((int32_t)ring[2*i+1] >= (int32_t)ring[2*(i-1)+1]);
        CHECK_EQUAL(0u, ring[2*i] & 255u);
    }
    CHECK_EQUAL(0u, ring[30]); CHECK_EQUAL(0u, ring[31]);
}
static void fade_in_wraps_without_touching_other_frames(void)
{
    uint32_t ring[16];
    for (int i = 0; i < 16; ++i) ring[i] = 0x10000000u;
    pcm_transition(ring, 8, 6, 4, true);
    CHECK_EQUAL(0u, ring[12]); CHECK_EQUAL(0u, ring[13]);
    CHECK(ring[14] < ring[0]); CHECK(ring[0] < ring[2]);
    CHECK_EQUAL(0x10000000u, ring[2]);
    for (int i = 4; i < 12; ++i) CHECK_EQUAL(0x10000000u, ring[i]);
    pcm_transition(ring, 8, 0, 0, false);
    CHECK_EQUAL(0x10000000u, ring[4]);
}
static void fade_out_stays_silent_instead_of_returning_to_full_level(void)
{
    uint32_t ring[32];
    for (int i = 0; i < 32; ++i) ring[i] = 0x40000000u;
    pcm_quiet_tail(ring, 16, 10, 4, 6);
    CHECK_EQUAL(0x40000000u, ring[20]);
    CHECK(ring[22] < ring[20]);
    CHECK(ring[24] < ring[22]);
    for (int frame = 13; frame < 20; ++frame) {
        CHECK_EQUAL(0u, ring[(frame % 16) * 2]);
        CHECK_EQUAL(0u, ring[(frame % 16) * 2 + 1]);
    }
    CHECK_EQUAL(0x40000000u, ring[12]);
    CHECK_EQUAL(0x40000000u, ring[18]);
}

static void resuming_fades_real_pcm_after_the_silent_tail(void)
{
    uint32_t ring[32];
    for (int i = 0; i < 32; ++i) ring[i] = 0x40000000u;
    pcm_quiet_tail(ring, 16, 10, 4, 6);
    pcm_transition(ring, 16, 4, 4, true);
    for (int frame = 13; frame <= 20; ++frame)
        CHECK_EQUAL(0u, ring[(frame % 16) * 2]);
    CHECK(ring[10] > 0);
    CHECK(ring[10] < ring[12]);
    CHECK(ring[12] < ring[14]);
    CHECK_EQUAL(0x40000000u, ring[14]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fades_preserve_polarity_and_end_at_silence),
        TEST_CASE(fade_in_wraps_without_touching_other_frames),
        TEST_CASE(fade_out_stays_silent_instead_of_returning_to_full_level),
        TEST_CASE(resuming_fades_real_pcm_after_the_silent_tail),
    };
    return test_run_all(cases, sizeof cases / sizeof cases[0]);
}
