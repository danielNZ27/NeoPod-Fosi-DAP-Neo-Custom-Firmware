/* Host probe: decode a real file through the firmware codec to s32le PCM.
 * Build with src/codec/flac.c and src/codec/flac_bits.c, codec/include on -I.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codec/flac.h"

typedef struct Input { unsigned char *data; uint32_t size; } Input;
static FlacDecoder decoder;
static unsigned lpc_jobs;
static unsigned widths[33];
static bool read_input(void *context, uint32_t offset, void *out, uint32_t length)
{
    Input *input = context;
    if (offset > input->size || length > input->size - offset) return false;
    memcpy(out, input->data + offset, length);
    return true;
}
static bool log_lpc(void *context, int32_t *samples, FlacLpcJob job)
{
    (void)context; (void)samples;
    lpc_jobs++;
    if (job.width <= 32) widths[job.width]++;
    return false;
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length <= 0 || (unsigned long)length > UINT32_MAX) return 2;
    Input input = {malloc((size_t)length), (uint32_t)length};
    if (!input.data || fread(input.data, 1, input.size, file) != input.size) return 2;
    fclose(file);
    FlacResult result = flac_open(&decoder, (FlacInput){read_input, &input, input.size});
    if (result != FLAC_OK) { fprintf(stderr, "open result=%d\n", result); return 1; }
    printf("rate=%u bits=%u channels=%u block=%u..%u total=%u\n",
           decoder.info.sample_rate, decoder.info.bits_per_sample, decoder.info.channels,
           decoder.info.min_block, decoder.info.max_block, decoder.info.total_samples);
    flac_set_lpc_hook(&decoder, (FlacLpcHook){log_lpc, NULL});
    FILE *output = fopen(argv[2], "wb");
    if (!output) return 2;
    unsigned frames = 0;
    uint32_t samples = 0;
    int32_t interleaved[FLAC_MAX_BLOCK * FLAC_MAX_CHANNELS];
    while ((result = flac_decode_frame(&decoder)) == FLAC_OK) {
        for (uint32_t i = 0; i < decoder.block_samples; i++)
            for (unsigned ch = 0; ch < decoder.info.channels; ch++)
                interleaved[i * decoder.info.channels + ch] =
                    (int32_t)((uint32_t)flac_channel(&decoder, (int)ch)[i] <<
                              (32 - decoder.info.bits_per_sample));
        size_t words = decoder.block_samples * decoder.info.channels;
        if (fwrite(interleaved, sizeof(int32_t), words, output) != words) return 2;
        samples += decoder.block_samples;
        frames++;
    }
    fclose(output);
    free(input.data);
    printf("result=%d frames=%u samples=%u LPC=%u\n", result, frames, samples, lpc_jobs);
    for (unsigned i = 0; i <= 32; i++)
        if (widths[i]) printf("LPC width %u: %u\n", i, widths[i]);
    return result == FLAC_END && samples == decoder.info.total_samples ? 0 : 1;
}
