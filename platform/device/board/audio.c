#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "registers.h"
#include "core/format.h"
#include "core/string_builder.h"
#include "core1_region.h"
#include "dac.h"
#include "device/audio_engine.h"
#include "device/audio_math.h"
#include "device/pcm_transition.h"
#include "device/dac_sequence.h"
#include "device/dma_loop.h"
#include "device/flac_source.h"
#include "device/aac_probe.h"
#include "device/mp3_probe.h"
#include "device/opus_probe.h"
#include "device/wav_source.h"
#include "dmac.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"

enum {
    RING_BLOCKS = 7,
    BLOCK_WORDS = 4000,
    RING_WORDS = RING_BLOCKS * BLOCK_WORDS,
    WORDS_PER_FRAME = 2,
    RING_FRAMES = RING_WORDS / WORDS_PER_FRAME,
    PREFILL_DIVISOR = 4,
    PREFILL_TIMEOUT_MS = 1000,
    FILL_BUDGET_BYTES = 32768,
    IDLE_SHUTDOWN_MS = 3000,
    AUDIO_CLOCK_MHZ = 200,
    TONE_PERIOD_SAMPLES = 100,
    TONE_AMPLITUDE = 9000,
    MILLISECONDS_PER_SECOND = 1000,
    JACK_POLL_MS = 10,
    SNIFF_BYTES = 36
};

typedef enum SourceKind {
    SOURCE_FLAC,
    SOURCE_WAV,
    SOURCE_MP3,
    SOURCE_AAC,
    SOURCE_OPUS,
    SOURCE_UNKNOWN
} SourceKind;

static union {
    FlacDecoder flac;
    WavBuffers wav;
} decoding __attribute__((section(".high_ram")));
static uint32_t music_ring[RING_WORDS];
static WavSource wav_source;
static SourceKind source_kind;
static bool unknown_format;
static bool output_failed;

static FlacSource software_source;
static Core1Source core1_source;
static bool using_core1;
#ifdef ECHO_BOARD_NEO
static uint8_t sniff_magic[4];
static HalToneResult neo_output_result;
static void neo_audio_log(const char *path, const char *reason)
{
    static char text[4096];
    StringBuilder b = string_builder_make(text, sizeof text);
    string_builder_append(&b, "EchoPod Neo V147 playback diagnostic\nreason=");
    string_builder_append(&b, reason);
    string_builder_append(&b, "\npath=");
    string_builder_append(&b, path);
    string_builder_append(&b, "\nmagic=");
    for (unsigned i=0; i<4; i++) {
        static const char hex[] = "0123456789abcdef";
        string_builder_append_char(&b, hex[sniff_magic[i] >> 4]);
        string_builder_append_char(&b, hex[sniff_magic[i] & 15u]);
    }
    string_builder_append(&b, "\ncore1=");
    string_builder_append_unsigned(&b, using_core1 ? 1u : 0u);
    string_builder_append(&b, "\noutput_result=");
    string_builder_append_unsigned(&b, (uint32_t)neo_output_result);
    HalToneDetail d = hal_audio_tone_detail();
    string_builder_append(&b, "\ni2c_stage=");
    string_builder_append_unsigned(&b, d.stage);
    string_builder_append(&b, "\ni2c_pending=");
    string_builder_append_unsigned(&b, d.pending);
    string_builder_append(&b, "\nprobe_gpio1_output=");
    string_builder_append_unsigned(&b, dac_path_probe_gpio1());
    string_builder_append(&b, "\nprobe_gpio2_output=");
    string_builder_append_unsigned(&b, dac_path_probe_gpio2());
    string_builder_append(&b, "\nprobe_gpio2_direction=");
    string_builder_append_unsigned(&b, dac_path_probe_gpio2_direction());
    string_builder_append(&b, "\ndevice_id=");
    string_builder_append_unsigned(&b, dac_path_device_id());
    string_builder_append(&b, "\ngpio0_output=");
    string_builder_append_unsigned(&b, register_read(GPIO0_BASE + GPIO_DATA));
    string_builder_append(&b, "\ngpio0_direction=");
    string_builder_append_unsigned(&b, register_read(GPIO0_BASE + GPIO_DIRECTION));
    string_builder_append(&b, "\ngpio2_output=");
    string_builder_append_unsigned(&b, register_read(GPIO2_BASE + GPIO_DATA));
    string_builder_append(&b, "\ngpio2_direction=");
    string_builder_append_unsigned(&b, register_read(GPIO2_BASE + GPIO_DIRECTION));
    string_builder_append(&b, "\ngpio2_input=");
    string_builder_append_unsigned(&b, register_read(GPIO2_BASE + GPIO_INPUT_LEVEL));
    string_builder_append(&b, "\ni2c0_pin_group=");
    string_builder_append_unsigned(&b, register_read(GRF_BASE + 0xBCu));
    string_builder_append(&b, "\n");
    for (size_t i=b.length; i<sizeof text; i++) text[i]='\n';
    (void)hal_file_write("/.echo/neo-audio.txt", text, sizeof text);
}
#endif
static bool resume_after_clock_change;
static bool paused_before_clock_change;
static uint32_t position_before_clock_change;
static AudioEngine engine;
static DmaItem items[RING_BLOCKS];
static DmaItem silence_item, resume_item;
static uint32_t silence_words[64];
static bool silence_active;
static bool quiet_tail_valid;
static uint32_t quiet_tail_end;
static HalFile file;
static bool engine_ready;
static bool path_up;
static uint32_t path_rate;
static bool transferring;
static uint32_t consumed;
static uint32_t last_position;
static uint32_t ring_words;
static bool stopped_waiting;
static uint32_t stopped_since_ms;
static bool tone_active;
static char last_path[AUDIO_ENGINE_PATH_CAPACITY];
static uint8_t volume_level;
static uint8_t extra_attenuation;
static void (*stream_service)(void);
static bool volume_set;
static HalAudioOutput output_setting;
static uint32_t last_jack_poll_ms;
static HalAudioStats stats;
static bool dsp_bypass_enabled;
static uint32_t read_us_pending;
static uint32_t output_started_us;

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const HalFile *open = context;
    uint32_t started_us = hal_clock_microseconds();
    bool read = hal_file_read(*open, offset, buffer, length);
    uint32_t read_us = hal_clock_microseconds() - started_us;
    read_us_pending += read_us;
    stats.read_total_us += read_us;
    return read;
}

static uint32_t larger(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static void pump(uint32_t consumed_frames)
{
    uint32_t frames = audio_engine_stats(&engine).frames;
    read_us_pending = 0;
    uint32_t decode_started_us = hal_clock_microseconds();
    if (using_core1) {
        core1_source_fill(&core1_source);
    }
    audio_engine_pump(&engine, consumed_frames);
    if (using_core1) {
        core1_source_fill(&core1_source);
    }
    uint32_t elapsed_us = hal_clock_microseconds() - decode_started_us;
    stats.busy_us += elapsed_us;
    if (audio_engine_stats(&engine).frames == frames) {
        return;
    }
    stats.blocks++;
    stats.decode_us = elapsed_us;
    stats.decode_max_us = larger(stats.decode_max_us, stats.decode_us);
    stats.decode_total_us += stats.decode_us;
    stats.read_us = read_us_pending;
    stats.read_max_us = larger(stats.read_max_us, stats.read_us);
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    if (!hal_file_open(&file, path)) {
        return false;
    }
    *input = (FlacInput){.read = read_file, .context = &file, .size = file.size};
    return true;
}

static void close_file(void *context)
{
    (void)context;
    hal_file_close(&file);
}

static bool tag_is(const uint8_t *bytes, const char *tag)
{
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
           bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

static SourceKind kind_of(const char *path)
{
    FlacInput input;
    if (!open_file(NULL, path, &input)) {
        return SOURCE_FLAC;
    }
    uint8_t magic[SNIFF_BYTES] = {0};
    bool read = input.size >= SNIFF_BYTES && input.read(input.context, 0, magic, sizeof magic);
#ifdef ECHO_BOARD_NEO
    for (unsigned i=0; i<4; i++) sniff_magic[i] = magic[i];
#endif
    close_file(NULL);
    if (!read || tag_is(magic, "fLaC")) {
        return SOURCE_FLAC;
    }
    if (tag_is(magic, "RIFF") && tag_is(magic + 8, "WAVE")) {
        return SOURCE_WAV;
    }
    if (mp3_signature(magic)) {
        return SOURCE_MP3;
    }
    if (opus_signature(magic, sizeof magic)) {
        return SOURCE_OPUS;
    }
    return aac_signature(magic, sizeof magic) ? SOURCE_AAC : SOURCE_UNKNOWN;
}

static AudioSource chosen_source(void)
{
    if (source_kind == SOURCE_WAV) {
        return wav_source_interface(&wav_source);
    }
    if (source_kind == SOURCE_MP3) {
        return core1_source_mp3_interface(&core1_source);
    }
    if (source_kind == SOURCE_AAC) {
        return core1_source_aac_interface(&core1_source);
    }
    if (source_kind == SOURCE_OPUS) {
        return core1_source_opus_interface(&core1_source);
    }
    return using_core1 ? core1_source_interface(&core1_source)
                       : flac_source_interface(&software_source);
}

static void remember_path(const char *path)
{
    uint32_t index = 0;
    while (path[index] != '\0' && index + 1 < AUDIO_ENGINE_PATH_CAPACITY) {
        last_path[index] = path[index];
        index++;
    }
    last_path[index] = '\0';
}

const char *board_audio_last_path(void)
{
    return last_path[0] != '\0' ? last_path : NULL;
}

static bool open_chosen(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)context;
    remember_path(path);
    source_kind = kind_of(path);
    unknown_format = source_kind == SOURCE_UNKNOWN;
    if (unknown_format) {
        return false;
    }
    if (source_kind == SOURCE_FLAC) {
        using_core1 = board_core1_start_stream(CORE1_PROGRAM_FLAC);
    }
    if (source_kind == SOURCE_MP3) {
        using_core1 = board_core1_start_stream(CORE1_PROGRAM_MP3);
        if (!using_core1) {
            return false;
        }
    }
    if (source_kind == SOURCE_AAC) {
        using_core1 = board_core1_start_stream(CORE1_PROGRAM_AAC);
        if (!using_core1) {
            return false;
        }
    }
    if (source_kind == SOURCE_OPUS) {
        using_core1 = board_core1_start_stream(CORE1_PROGRAM_OPUS);
        if (!using_core1) {
            return false;
        }
    }
    AudioSource source = chosen_source();
    bool opened = source.open(source.context, path, start_ms, track);
#ifdef ECHO_BOARD_NEO
    if (!opened && source_kind == SOURCE_FLAC && using_core1) {
        source.close(source.context);
        board_core1_stop();
        using_core1 = false;
        source = chosen_source();
        opened = source.open(source.context, path, start_ms, track);
    }
#endif
    return opened;
}

static AudioFrameResult next_chosen(void *context, AudioFrame *frame)
{
    (void)context;
    AudioSource source = chosen_source();
    return source.next(source.context, frame);
}

static void release_chosen(void *context)
{
    (void)context;
    AudioSource source = chosen_source();
    source.release(source.context);
}

static void close_chosen(void *context)
{
    (void)context;
    AudioSource source = chosen_source();
    source.close(source.context);
}

static uint32_t *ring(void)
{
    return music_ring;
}

static void ensure_engine(void)
{
    if (engine_ready) {
        return;
    }
    FlacFileOpener files = {.open = open_file, .close = close_file, .context = NULL};
    flac_source_init(&software_source, files, &decoding.flac);
    core1_source_init(&core1_source, core1_region(), files, &decoding.flac, FILL_BUDGET_BYTES);
    wav_source_init(&wav_source, files, &decoding.wav);
    AudioSource source = {open_chosen, next_chosen, release_chosen, close_chosen, NULL};
    audio_engine_init(
        &engine, (AudioEngineConfig){.source = source, .ring = ring(), .ring_words = RING_WORDS});
    engine_ready = true;
}

void *hal_system_borrow_listing(uint32_t *size)
{
    board_audio_shut_down();
    *size = (uint32_t)sizeof music_ring;
    return music_ring;
}

static uint32_t ring_address(void)
{
    return (uint32_t)(uintptr_t)music_ring;
}

static void update_consumed(void)
{
    if (!transferring) {
        return;
    }
    uint32_t frames = ring_words / WORDS_PER_FRAME;
    uint32_t position =
        dma_loop_position(dmac_source(), ring_address(), ring_words) / WORDS_PER_FRAME;
    consumed += (position + frames - last_position) % frames;
    last_position = position;
}

static void serve_fade(void)
{
    if (stream_service != NULL) {
        stream_service();
    } else if (engine_ready && transferring && !tone_active) {
        update_consumed();
        pump(consumed);
    }
}

static void bluetooth_fade_in(uint32_t start, uint32_t frames)
{
#ifdef ECHO_BOARD_NEO
    if (board_bluetooth_routed() || silence_active)
        pcm_transition(music_ring, RING_FRAMES, start, frames, true);
#else
    (void)start;
    (void)frames;
#endif
}

void board_audio_prepare_bluetooth_start(void)
{
#ifdef ECHO_BOARD_NEO
    if (!transferring || !engine_ready || tone_active || stream_service != NULL ||
        audio_engine_status(&engine).state != AUDIO_ENGINE_PLAYING) return;
    update_consumed();
    uint32_t guard = path_rate / 200;
    uint32_t quiet = path_rate / 100;
    uint32_t ramp = path_rate / 100;
    if (audio_engine_buffered(&engine, consumed) <= guard + quiet + ramp) return;
    uint32_t start = (last_position + guard) % RING_FRAMES;
    pcm_quiet_tail(music_ring, RING_FRAMES, start, 0, quiet);
    pcm_transition(music_ring, RING_FRAMES, (start + quiet) % RING_FRAMES, ramp, true);
    board_delay_ms(7);
#endif
}

static void bluetooth_quiet(bool keep_clock)
{
    quiet_tail_valid = false;
#ifdef ECHO_BOARD_NEO
    if (board_bluetooth_routed()) {
        if (transferring && engine_ready && !tone_active && stream_service == NULL &&
            audio_engine_status(&engine).state != AUDIO_ENGINE_PAUSED) {
            update_consumed();
            uint32_t available = audio_engine_buffered(&engine, consumed);
            uint32_t guard = path_rate / 200; /* Stay ahead of the live DMA reader. */
            uint32_t frames = path_rate / 100;
            if (available > guard + frames) {
                uint32_t at = (last_position + guard) % RING_FRAMES;
                pcm_quiet_tail(music_ring, RING_FRAMES, at, frames, path_rate / 100);
                quiet_tail_end = (at + frames + path_rate / 100) % RING_FRAMES;
                quiet_tail_valid = true;
                board_delay_ms(17);
                update_consumed();
            }
        }
        if (keep_clock) return;
        board_bluetooth_audio_mute(true);
        /* Send CY0/0 and let the module mute before stopping its I2S source. */
        board_delay_ms(20);
    } else {
        board_bluetooth_audio_mute(true);
    }
#endif
    (void)keep_clock;
}

static bool start_silence(void)
{
    dmac_suspend();
    update_consumed();
    if (engine_ready) audio_engine_observe(&engine, consumed);
    dmac_stop();
    silence_item = dma_loop_item((uint32_t)(uintptr_t)silence_words, 64,
                                  (uint32_t)(uintptr_t)&silence_item);
    silence_active = dmac_start_loop(&silence_item);
    transferring = false;
    if (silence_active) dac_path_start_transfer();
    return silence_active;
}

static void stop_transfer(void)
{
    if (!transferring) {
        return;
    }
    bool keep_clock = board_bluetooth_routed() && engine_ready && !tone_active && stream_service == NULL;
    bluetooth_quiet(keep_clock);
    if (keep_clock && start_silence()) {
        dac_path_set_muted(true);
        return;
    }
    if (keep_clock) {
        board_bluetooth_audio_mute(true);
        board_delay_ms(20);
    }
    dac_path_set_muted(true);
    dac_path_stop_transfer();
    dmac_stop();
    transferring = false;
}

static void stop_path(void)
{
    stop_transfer();
    if (path_up && board_bluetooth_routed()) {
        board_bluetooth_audio_mute(true);
        board_delay_ms(20);
    }
    if (silence_active) {
        dmac_stop();
        silence_active = false;
    }
    if (path_up) {
        dac_path_stop();
        path_up = false;
    }
}

static bool ensure_path(uint32_t rate)
{
    if (path_up && path_rate == rate) {
        return true;
    }
    stop_path();
    HalToneResult result = dac_path_start(rate);
#ifdef ECHO_BOARD_NEO
    neo_output_result = result;
#endif
    if (result != HAL_TONE_PLAYED) {
        return false;
    }
    path_up = true;
    path_rate = rate;
    dac_path_output_on();
    return true;
}

static bool start_transfer(uint32_t words, bool audible)
{
    if (audible) bluetooth_fade_in(0, path_rate / 100);
    if (silence_active) {
        dmac_stop();
        silence_active = false;
    }
    quiet_tail_valid = false;
    ring_words = words;
    consumed = 0;
    last_position = 0;
    uint32_t blocks = words / BLOCK_WORDS;
    if (!dma_ring_items(items, blocks, ring_address(), BLOCK_WORDS, (uint32_t)(uintptr_t)items) ||
        !dmac_start_loop(&items[0])) {
        return false;
    }
    dac_path_start_transfer();
    transferring = true;
    dac_path_set_fade_service(serve_fade);
    if (audible) {
        dac_path_set_muted(false);
        board_bluetooth_audio_mute(false);
    }
    return true;
}

static bool start_output(bool audible)
{
    if (!ensure_path(audio_engine_rate(&engine))) {
        return false;
    }
    uint32_t started_ms = hal_clock_milliseconds();
    uint32_t prefill = audio_engine_rate(&engine) / PREFILL_DIVISOR;
    prefill = prefill < RING_FRAMES * 3 / 4 ? prefill : RING_FRAMES * 3 / 4;
    while (audio_engine_buffered(&engine, 0) < prefill &&
           hal_clock_milliseconds() - started_ms < PREFILL_TIMEOUT_MS) {
        pump(0);
    }
    stats = (HalAudioStats){0};
    output_started_us = hal_clock_microseconds();
    stopped_waiting = false;
    if (!audible && board_bluetooth_routed()) {
        quiet_tail_valid = false;
        ring_words = RING_WORDS;
        consumed = last_position = 0;
        if (!dma_ring_items(items, RING_BLOCKS, ring_address(), BLOCK_WORDS, (uint32_t)(uintptr_t)items))
            return false;
        if (silence_active) return true;
        return start_silence();
    }
    return start_transfer(RING_WORDS, audible);
}

static void stop_tone(void)
{
    if (tone_active) {
        stop_path();
        tone_active = false;
    }
}

static void ensure_audio_clock(void)
{
    if (hal_system_clock_mhz() < AUDIO_CLOCK_MHZ) {
        hal_system_set_clock_mhz(AUDIO_CLOCK_MHZ);
    }
}

bool hal_audio_play(const char *path, uint32_t start_ms, uint32_t token)
{
    ensure_engine();
    ensure_audio_clock();
    stop_tone();
    stop_transfer();
    output_failed = false;
    if (!audio_engine_play(&engine, path, start_ms, token)) {
#ifdef ECHO_BOARD_NEO
        neo_audio_log(path, "file or decoder open failed");
#endif
        return false;
    }
    if (!start_output(true)) {
        output_failed = true;
        audio_engine_stop(&engine);
#ifdef ECHO_BOARD_NEO
        neo_audio_log(path, "DAC or audio output start failed");
#endif
        return false;
    }
    return true;
}

void hal_audio_queue_next(const char *path, uint32_t token)
{
    ensure_engine();
    audio_engine_queue_next(&engine, path, token);
}

void hal_audio_pause(void)
{
    ensure_engine();
    if (transferring && board_bluetooth_routed()) {
        stop_transfer();
        audio_engine_pause(&engine);
        return;
    }
    if (transferring) {
        bluetooth_quiet(false);
        dac_path_set_muted(true);
        dmac_suspend();
    }
    audio_engine_pause(&engine);
}

void hal_audio_resume(void)
{
    ensure_engine();
    audio_engine_resume(&engine);
    if (silence_active) {
        uint32_t fade_start = last_position;
        if (quiet_tail_valid && (quiet_tail_end + RING_FRAMES - last_position) % RING_FRAMES <= path_rate / 50)
            fade_start = quiet_tail_end;
        bluetooth_fade_in(fade_start, path_rate / 100);
        quiet_tail_valid = false;
        dmac_stop();
        silence_active = false;
        if (!dma_ring_resume_item(&resume_item, RING_BLOCKS, ring_address(), BLOCK_WORDS,
                                  (uint32_t)(uintptr_t)items, last_position * WORDS_PER_FRAME) ||
            !dmac_start_loop(&resume_item)) {
            output_failed = true;
            audio_engine_stop(&engine);
            stop_path();
            return;
        }
        transferring = true;
        dac_path_start_transfer();
        dac_path_set_muted(false);
        board_bluetooth_audio_mute(false);
        return;
    }
    if (transferring) {
        uint32_t position = dma_loop_position(dmac_source(), ring_address(), ring_words) / WORDS_PER_FRAME;
        bluetooth_fade_in(position, path_rate / 100);
        dmac_resume();
        dac_path_set_muted(false);
        board_bluetooth_audio_mute(false);
    }
}

static void seek_to(uint32_t position_ms, bool paused)
{
    ensure_engine();
    ensure_audio_clock();
    stop_transfer();
    if (!audio_engine_seek(&engine, position_ms) || !start_output(!paused)) {
        audio_engine_stop(&engine);
        return;
    }
    if (paused) {
        audio_engine_pause(&engine);
        dac_path_set_muted(true);
        if (!silence_active) dmac_suspend();
    }
}

void hal_audio_seek(uint32_t position_ms)
{
    ensure_engine();
    seek_to(position_ms, audio_engine_status(&engine).state == AUDIO_ENGINE_PAUSED);
}

static DacCurve output_curve(void)
{
#ifdef ECHO_BOARD_NEO
    return DAC_CURVE_SINGLE_ENDED;
#else
    switch (output_setting) {
    case HAL_AUDIO_OUTPUT_SINGLE_ENDED:
        return DAC_CURVE_SINGLE_ENDED;
    case HAL_AUDIO_OUTPUT_BALANCED:
        return DAC_CURVE_BALANCED;
    case HAL_AUDIO_OUTPUT_AUTO:
        break;
    }
    return hal_audio_jack() == HAL_AUDIO_JACK_BALANCED ? DAC_CURVE_BALANCED
                                                       : DAC_CURVE_SINGLE_ENDED;
#endif
}

static void apply_volume(void)
{
    if (volume_set) {
        uint32_t value = dac_volume_value(volume_level, output_curve()) + extra_attenuation;
        dac_path_set_volume(value < DAC_VOLUME_MUTED ? (uint8_t)value : DAC_VOLUME_MUTED);
    }
}

void hal_audio_set_volume(uint8_t level)
{
    volume_level = level;
    volume_set = true;
    apply_volume();
}

void hal_audio_set_gain(HalAudioGain gain)
{
    dac_path_set_gain(gain == HAL_AUDIO_GAIN_HIGH);
}

void hal_audio_set_output(HalAudioOutput output)
{
    output_setting = output;
    apply_volume();
}

void hal_audio_set_eq(bool enabled, const int8_t *gains)
{
    ensure_engine();
    audio_engine_set_eq(&engine, enabled, gains);
}

void hal_audio_set_replay_gain_mode(ReplayGainMode mode)
{
    ensure_engine();
    audio_engine_set_replay_gain_mode(&engine, mode);
}

ReplayGainMode hal_audio_replay_gain_mode(void)
{
    ensure_engine();
    return audio_engine_replay_gain_mode(&engine);
}

void hal_audio_set_dsp_bypass(bool bypass)
{
    dsp_bypass_enabled = bypass;
    ensure_engine();
    audio_engine_set_dsp_bypass(&engine, bypass);
}

bool hal_audio_dsp_bypass(void)
{
    return dsp_bypass_enabled;
}

static HalAudioState state_of(AudioEngineState state)
{
    switch (state) {
    case AUDIO_ENGINE_PLAYING:
        return HAL_AUDIO_PLAYING;
    case AUDIO_ENGINE_PAUSED:
        return HAL_AUDIO_PAUSED;
    case AUDIO_ENGINE_STOPPED:
        return HAL_AUDIO_STOPPED;
    }
    return HAL_AUDIO_STOPPED;
}

static HalAudioCodec codec_of(AudioCodec codec)
{
    switch (codec) {
    case AUDIO_CODEC_WAV:
        return HAL_AUDIO_CODEC_WAV;
    case AUDIO_CODEC_MP3:
        return HAL_AUDIO_CODEC_MP3;
    case AUDIO_CODEC_AAC:
        return HAL_AUDIO_CODEC_AAC;
    case AUDIO_CODEC_OPUS:
        return HAL_AUDIO_CODEC_OPUS;
    case AUDIO_CODEC_FLAC:
        break;
    }
    return HAL_AUDIO_CODEC_FLAC;
}

HalAudioRefusal hal_audio_refusal(void)
{
    ensure_engine();
    if (output_failed) {
        return (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_OUTPUT};
    }
    AudioRefusal refusal = audio_engine_refusal(&engine);
    switch (refusal.problem) {
    case AUDIO_REFUSAL_RATE:
        return (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE,
                                 .sample_rate = refusal.sample_rate};
    case AUDIO_REFUSAL_UNREADABLE:
        return (HalAudioRefusal){.problem = unknown_format ? HAL_AUDIO_PROBLEM_FORMAT
                                                           : HAL_AUDIO_PROBLEM_UNREADABLE};
    case AUDIO_REFUSAL_NONE:
        break;
    }
    return (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_NONE};
}

static HalAudioFormat format_of(AudioTrackInfo track)
{
    FlacInfo stream = track.stream;
    if (stream.sample_rate == 0) {
        return (HalAudioFormat){.codec = HAL_AUDIO_CODEC_UNKNOWN};
    }
    return (HalAudioFormat){
        .codec = codec_of(track.codec),
        .sample_rate = stream.sample_rate,
        .bits_per_sample = stream.bits_per_sample,
        .channels = stream.channels,
        .bitrate_kbps = average_kbps(track.audio_bytes, stream.total_samples, stream.sample_rate),
    };
}

static bool output_idle(void)
{
    return !path_up && !board_core1_streaming();
}

HalAudioStatus hal_audio_status(void)
{
    ensure_engine();
    update_consumed();
    audio_engine_observe(&engine, consumed);
    AudioEngineStatus status = audio_engine_status(&engine);
    return (HalAudioStatus){.state = state_of(status.state),
                            .token = status.token,
                            .position_ms = status.position_ms,
                            .failed = status.failed,
                            .idle = output_idle(),
                            .format = format_of(status.track)};
}

static void change_rate(void)
{
    uint32_t rate = 0;
    if (!audio_engine_wants_rate(&engine, &rate)) {
        return;
    }
    stop_transfer();
    audio_engine_restart(&engine);
    if (!start_output(true)) {
        audio_engine_stop(&engine);
    }
}

static void shut_down_when_idle(void)
{
    if (audio_engine_status(&engine).state != AUDIO_ENGINE_STOPPED) {
        stopped_waiting = false;
        return;
    }
    uint32_t now_ms = hal_clock_milliseconds();
    if (!stopped_waiting) {
        stopped_waiting = true;
        stopped_since_ms = now_ms;
        return;
    }
    if (now_ms - stopped_since_ms >= IDLE_SHUTDOWN_MS) {
        stop_path();
        board_core1_stop();
        using_core1 = false;
        stopped_waiting = false;
    }
}

static void poll_jack(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    if (now_ms - last_jack_poll_ms < JACK_POLL_MS) {
        return;
    }
    last_jack_poll_ms = now_ms;
    if (board_jack_poll() && output_setting == HAL_AUDIO_OUTPUT_AUTO) {
        apply_volume();
    }
}

void hal_audio_service(void)
{
    poll_jack();
    if ((transferring || tone_active) && dac_path_take_underrun()) {
        stats.output_underruns++;
    }
    if (stream_service != NULL) {
        stream_service();
        return;
    }
    if (silence_active) {
        shut_down_when_idle();
        return;
    }
    if (!engine_ready || !transferring || tone_active) {
        return;
    }
    update_consumed();
    pump(consumed);
    change_rate();
    shut_down_when_idle();
}

HalAudioStats hal_audio_stats(void)
{
    HalAudioStats result = stats;
    result.transferring = transferring;
    result.dma_running = dmac_running();
    result.dma_sar = dmac_source();
    result.dac_volume = dac_path_applied_volume();
    result.dac_muted = dac_path_is_muted();
    result.dac_up = path_up;
    result.dac_rate = path_rate;
    result.dsp_bypass = dsp_bypass_enabled;
    if (engine_ready) {
        result.produced = engine.produced;
        result.eq_enabled = engine.eq.enabled;
        result.eq_flat = eq_is_flat(&engine.eq);
        result.replay_gain_mode = engine.replay_gain_mode;
        result.replay_gain_mult = engine.replay_gain_mult;
        result.underruns = audio_engine_stats(&engine).underruns;
    }
    if (!engine_ready || !transferring || tone_active) {
        return result;
    }
    result.consumed = consumed;
    result.elapsed_us = hal_clock_microseconds() - output_started_us;
    uint32_t rate = audio_engine_rate(&engine);
    if (rate > 0) {
        result.buffered_ms = audio_engine_buffered(&engine, consumed) * (uint32_t)MILLISECONDS_PER_SECOND / rate;
    }
    return result;
}

HalToneResult hal_audio_dma_tone_start(uint32_t sample_rate)
{
    ensure_engine();
    stop_tone();
    stop_transfer();
    audio_engine_stop(&engine);
    if (!dmac_present()) {
        return HAL_TONE_NO_DMAC;
    }
    stop_path();
    HalToneResult result = dac_path_start(sample_rate);
    if (result != HAL_TONE_PLAYED) {
        return result;
    }
    path_up = true;
    path_rate = sample_rate;
    uint32_t *words = ring();
    int16_t samples[TONE_PERIOD_SAMPLES];
    tone_fill(samples, TONE_PERIOD_SAMPLES, TONE_AMPLITUDE);
    for (uint32_t index = 0; index < RING_WORDS; index++) {
        uint32_t frame = index / WORDS_PER_FRAME;
        words[index] = (uint32_t)(uint16_t)samples[frame % TONE_PERIOD_SAMPLES] << 16;
    }
    dac_path_output_on();
    tone_active = true;
    if (!start_transfer(RING_WORDS, true)) {
        tone_active = false;
        stop_path();
        return HAL_TONE_NO_DMAC;
    }
    return HAL_TONE_PLAYED;
}

void hal_audio_dma_tone_stop(void)
{
    stop_tone();
}

HalDmaToneStatus hal_audio_dma_tone_status(void)
{
    if (!tone_active) {
        return (HalDmaToneStatus){.running = false, .position = 0};
    }
    uint32_t position = dma_loop_position(dmac_source(), ring_address(), ring_words);
    return (HalDmaToneStatus){.running = dmac_running(), .position = position};
}

const char *board_audio_current_path(void)
{
    return engine_ready ? audio_engine_current_path(&engine) : NULL;
}

void board_audio_before_clock_change(void)
{
    resume_after_clock_change = false;
    if (!board_core1_streaming()) {
        return;
    }
    if (engine_ready && transferring) {
        HalAudioStatus status = hal_audio_status();
        resume_after_clock_change = status.state != HAL_AUDIO_STOPPED;
        paused_before_clock_change = status.state == HAL_AUDIO_PAUSED;
        position_before_clock_change = status.position_ms;
        audio_engine_stop(&engine);
    }
    board_core1_stop();
    using_core1 = false;
}

void board_audio_after_clock_change(void)
{
    if (!resume_after_clock_change) {
        return;
    }
    resume_after_clock_change = false;
    seek_to(position_before_clock_change, paused_before_clock_change);
}

void board_audio_shut_down(void)
{
    if (engine_ready) {
        audio_engine_stop(&engine);
    }
    stop_tone();
    stop_path();
    board_core1_stop();
    using_core1 = false;
}

uint8_t board_audio_volume_level(void)
{
    return volume_level;
}

void board_audio_set_attenuation(uint8_t steps)
{
    extra_attenuation = steps;
    apply_volume();
}

static void clear_ring(void)
{
    for (uint32_t index = 0; index < RING_WORDS; index++) {
        music_ring[index] = 0;
    }
}

bool board_audio_stream_open(uint32_t rate, void (*service)(void))
{
    ensure_engine();
    ensure_audio_clock();
    board_audio_shut_down();
    clear_ring();
    stream_service = service;
    return ensure_path(rate);
}

uint32_t *board_audio_stream_ring(uint32_t *frames)
{
    *frames = RING_FRAMES;
    return music_ring;
}

bool board_audio_stream_play(void)
{
    return start_transfer(RING_WORDS, true);
}

void board_audio_stream_pause(void)
{
    stop_transfer();
    clear_ring();
}

uint32_t board_audio_stream_consumed(void)
{
    update_consumed();
    return transferring ? consumed : 0;
}

void board_audio_stream_close(void)
{
    stream_service = NULL;
    board_audio_shut_down();
}
