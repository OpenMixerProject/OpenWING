#ifndef WING_SURFACE_COMMON_H
#define WING_SURFACE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#define WING_CSC_DEVICE "/dev/ttymxc4"
#define WING_CSC_BAUD 115200u
#define WING_PNLC_DEVICE "/dev/ttymxc3"
#define WING_PNLC_BAUD 115200u
#define WING_FRAME_STAR 0x2au

struct wing_frame_parser {
    uint8_t payload[512];
    size_t len;
    int in_frame;
    int after_star;
    int have_cmd;
    uint8_t cmd;
};

struct wing_surface {
    int csc_fd;
    int pnlc_fd;
    int csc_lights_enabled;
    uint8_t strip_led_states[36];
    uint8_t layer_led_states[52];
    uint8_t pnlc_led_states[18];
    uint8_t csc_led_brightness;
    uint8_t csc_lamp_brightness;
    uint8_t csc_glow_brightness;
    uint8_t csc_patch_brightness;
};

typedef void (*wing_frame_cb)(void *user, const char *source, uint8_t cmd,
                              const uint8_t *payload, size_t len, uint8_t check);

void wing_parser_reset(struct wing_frame_parser *parser);
void wing_parser_feed(struct wing_frame_parser *parser, const char *source, uint8_t byte,
                      wing_frame_cb cb, void *user);
uint8_t wing_frame_checksum(const uint8_t *payload, size_t len);

int wing_serial_open(const char *path, unsigned int baud);
int wing_serial_open_pnlc(const char *path);
int wing_send_frame(int fd, uint8_t cmd, const uint8_t *payload, size_t len);
int wing_init_csc_transport(void);
int wing_enable_csc_lights(uint32_t value);
int wing_csc_latch_update(unsigned int port, uint32_t value, uint32_t mask);
int wing_configure_pnlc_transport(void);
int wing_init_pnlc_transport(void);
int wing_send_stock_baseline(int fd);
int wing_enable_pnlc_touch(int fd);
int wing_enable_pnlc_backlight(int fd);

void wing_surface_init_state(struct wing_surface *surface);
int wing_surface_open(struct wing_surface *surface, const char *csc_device,
                      unsigned int csc_baud, const char *pnlc_device, int open_pnlc);
void wing_surface_close(struct wing_surface *surface);

int wing_surface_fader(struct wing_surface *surface, unsigned int fader_1_based,
                       unsigned int value);
int wing_surface_csc_lights(struct wing_surface *surface, uint32_t value);
int wing_surface_csc_brightness(struct wing_surface *surface, int led, int lamp, int glow, int patch);
int wing_surface_csc_latch(struct wing_surface *surface, unsigned int port, uint32_t value,
                           uint32_t mask);
int wing_surface_led(struct wing_surface *surface, const char *name, int on);
int wing_surface_led_color(struct wing_surface *surface, const char *name, const char *color,
                           int on);
int wing_surface_csc_leds_all(struct wing_surface *surface, int on);
int wing_surface_raw_csc(struct wing_surface *surface, uint8_t cmd, const uint8_t *payload,
                         size_t len);
int wing_surface_raw_pnlc(struct wing_surface *surface, uint8_t cmd, const uint8_t *payload,
                          size_t len);

int wing_surface_csc_text(struct wing_surface *surface, int style, const char *line1, const char *line2);
int wing_surface_csc_meters(struct wing_surface *surface, const uint8_t *levels);
int wing_surface_csc_meter_update(struct wing_surface *surface, int index, int value);


void wing_describe_frame(char *out, size_t out_len, const char *source, uint8_t cmd,
                         const uint8_t *payload, size_t len, uint8_t check);
int wing_parse_uint(const char *s, unsigned int max, unsigned int *out);
int wing_parse_hex_byte(const char *s, uint8_t *out);

int wing_surface_scribble_text(struct wing_surface *surface, unsigned int slot, int explicit_addr,
                              int inverted, uint16_t style, const char *text1, const char *text2);
int wing_surface_scribble_bitmap(struct wing_surface *surface, unsigned int slot,
                                int inverted, uint16_t style, const uint8_t *bitmap, size_t bitmap_len);

#endif

