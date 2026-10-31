#include "device/dma_loop.h"

#define I2S1_TXDR 0x40030024u
#define CONTROL_LOW_LOOPED 0x18909124u

enum {
    WORD_BYTES = 4
};

DmaItem dma_loop_item(uint32_t buffer, uint32_t words, uint32_t item_address)
{
    return (DmaItem){
        .source = buffer,
        .destination = I2S1_TXDR,
        .next = item_address,
        .control_low = CONTROL_LOW_LOOPED,
        .control_high = words,
        .unused = 0,
    };
}

bool dma_ring_items(DmaItem *items, uint32_t count, uint32_t buffer, uint32_t words_per_item,
                    uint32_t items_address)
{
    if (count == 0 || words_per_item == 0 || words_per_item > DMA_MAX_BLOCK_WORDS) {
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        uint32_t next = items_address + ((index + 1) % count) * (uint32_t)sizeof(DmaItem);
        items[index] =
            dma_loop_item(buffer + index * words_per_item * WORD_BYTES, words_per_item, next);
    }
    return true;
}

uint32_t dma_loop_position(uint32_t source, uint32_t buffer, uint32_t words)
{
    if (source < buffer) {
        return 0;
    }
    uint32_t position = (source - buffer) / WORD_BYTES;
    return position < words ? position : 0;
}

bool dma_ring_resume_item(DmaItem *item, uint32_t count, uint32_t buffer,
                          uint32_t words_per_item, uint32_t items_address, uint32_t offset_words)
{
    if (!count || !words_per_item || words_per_item > DMA_MAX_BLOCK_WORDS ||
        offset_words >= count * words_per_item || offset_words % 2) return false;
    uint32_t block = offset_words / words_per_item;
    uint32_t remaining = words_per_item - offset_words % words_per_item;
    uint32_t next = items_address + ((block + 1) % count) * (uint32_t)sizeof(DmaItem);
    *item = dma_loop_item(buffer + offset_words * WORD_BYTES, remaining, next);
    return true;
}
