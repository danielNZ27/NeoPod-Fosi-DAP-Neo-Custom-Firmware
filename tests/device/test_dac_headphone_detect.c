#include "check.h"
#include "device/dac_sequence.h"

typedef struct ProbeBus {
    uint8_t control, status;
    unsigned reads, writes, fail_read;
    bool fail_write, ignore_write;
} ProbeBus;

static bool read_probe(void *context, uint8_t address, uint32_t map, uint8_t *value)
{
    ProbeBus *bus = context;
    CHECK_EQUAL(0x66, address);
    CHECK(map == 0x0D0000 || map == 0x0D0001);
    if (++bus->reads == bus->fail_read) return false;
    *value = map == 0x0D0000 ? bus->control : bus->status;
    return true;
}

static bool write_probe(void *context, uint8_t address, uint32_t map, uint8_t value)
{
    ProbeBus *bus = context;
    CHECK_EQUAL(0x66, address);
    CHECK_EQUAL(0x0D0000, map);
    CHECK_EQUAL(bus->control | 0xC0, value);
    bus->writes++;
    if (bus->fail_write) return false;
    if (!bus->ignore_write) bus->control = value;
    return true;
}

static bool probe(ProbeBus *fake, uint8_t *control, uint8_t *status)
{
    DacBus bus = {.read = read_probe, .write = write_probe, .context = fake};
    return dac_headphone_detect_read(&bus, 0x66, control, status);
}

static void enables_detector_preserving_inversion_and_debounce(void)
{
    ProbeBus bus = {.control = 0x2A, .status = 0x40};
    uint8_t control = 0, status = 0;
    CHECK(probe(&bus, &control, &status));
    CHECK_EQUAL(0xEA, control);
    CHECK_EQUAL(0x40, status);
    CHECK_EQUAL(1, bus.writes);
    CHECK_EQUAL(3, bus.reads);
}

static void enabled_detector_is_only_read_and_returns_current_bits(void)
{
    ProbeBus bus = {.control = 0xC4, .status = 0x20};
    uint8_t control = 0, status = 0;
    CHECK(probe(&bus, &control, &status));
    CHECK_EQUAL(0xC4, control);
    CHECK_EQUAL(0x20, status);
    CHECK_EQUAL(0, bus.writes);
    CHECK_EQUAL(2, bus.reads);
}

static void failures_never_publish_a_valid_or_stale_sample(void)
{
    for (unsigned fail = 1; fail <= 3; fail++) {
        ProbeBus bus = {.control = 0x04, .status = 0x40, .fail_read = fail};
        uint8_t control = 0xAA, status = 0xBB;
        CHECK(!probe(&bus, &control, &status));
        CHECK_EQUAL(0xAA, control);
        CHECK_EQUAL(0xBB, status);
    }
    for (unsigned fail = 0; fail < 2; fail++) {
        ProbeBus bus = {.control = 0x04, .fail_write = fail == 0, .ignore_write = fail == 1};
        uint8_t control = 0xAA, status = 0xBB;
        CHECK(!probe(&bus, &control, &status));
        CHECK_EQUAL(0xAA, control);
        CHECK_EQUAL(0xBB, status);
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(enables_detector_preserving_inversion_and_debounce),
        TEST_CASE(enabled_detector_is_only_read_and_returns_current_bits),
        TEST_CASE(failures_never_publish_a_valid_or_stale_sample),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
