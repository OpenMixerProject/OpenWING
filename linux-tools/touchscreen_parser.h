#ifndef TOUCHSCREEN_PARSER_H
#define TOUCHSCREEN_PARSER_H

#include <stdint.h>
#include <stddef.h>

#define ESCAPE 0x2a
#define MAX_FRAME_BYTES 514

struct touch_decoder {
    uint8_t buf[256];
    size_t len;
    int in_frame;
    uint8_t type;
    int pending_star;
};

static inline int is_touchscreen_type(uint8_t b) {
    return b == 0x41 || b == 0x81 || b == 0x01 || b == 0xE1;
}

static inline void touch_decoder_reset(struct touch_decoder *decoder) {
    decoder->len = 0;
    decoder->in_frame = 0;
    decoder->type = 0;
    decoder->pending_star = 0;
}

#endif
