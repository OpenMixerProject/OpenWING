#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_FB_PATH "/dev/fb0"
#define DEFAULT_TOUCH_PATH "/dev/ttymxc3"
#include "touchscreen_parser.h"

struct framebuffer {
    int fd;
    uint8_t *ptr;
    size_t size;
    unsigned int bytes_per_pixel;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
};

struct app_state {
    struct framebuffer fb;
    unsigned int raw_width;
    unsigned int raw_height;
    unsigned int brush_radius;
    uint32_t color;
    bool touching;
    int last_x;
    int last_y;
    unsigned long frames_seen;
    unsigned long touch_events;
};

struct stock_touch_decoder {
    uint8_t payload[512];
    size_t len;
    bool in_frame;
    bool after_star;
    bool have_cmd;
    uint8_t cmd;
};

static volatile sig_atomic_t running = 1;
static struct termios saved_stdin_termios;
static bool saved_stdin_termios_valid;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Simple framebuffer drawing program for the Wing touchscreen.\n"
            "Run it from the USB serial shell, then draw on the LCD.\n"
            "\n"
            "Options:\n"
            "  -f, --fb PATH          Framebuffer device (default: %s)\n"
            "  -t, --touch PATH       PNLC touch UART (default: %s)\n"
            "      --raw-size WxH     Raw touch coordinate size (default: framebuffer size)\n"
            "      --brush PIXELS     Brush radius (default: 6)\n"
            "  -h, --help             Show this help\n"
            "\n"
            "\n"
            "wing-draw enables the stock PNLC touch-report mode and decodes\n"
            "stock 'p' touch frames and 2A 41/81/01/E1 raw touch reports.\n"
            "\n"
            "Serial-console keys while running: c=clear, q=quit, Ctrl-C=quit.\n",
            prog, DEFAULT_FB_PATH, DEFAULT_TOUCH_PATH);
}

static void handle_signal(int signo)
{
    (void)signo;
    running = 0;
}

static void restore_stdin_termios(void)
{
    if (saved_stdin_termios_valid)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_stdin_termios);
}

static void setup_stdin_raw(void)
{
    struct termios tio;

    if (!isatty(STDIN_FILENO))
        return;
    if (tcgetattr(STDIN_FILENO, &saved_stdin_termios) != 0)
        return;

    tio = saved_stdin_termios;
    tio.c_lflag &= ~(ICANON | ECHO);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) == 0) {
        saved_stdin_termios_valid = true;
        atexit(restore_stdin_termios);
    }
}

static uint8_t frame_checksum(const uint8_t *payload, size_t len)
{
    unsigned int sum = 0;

    for (size_t i = 0; i < len; i++)
        sum = (sum + payload[i]) & 0xffu;

    return (uint8_t)(((sum & 0xffu) ^ (len & 0xffu)) | 0x80u);
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_pnlc_frame(int fd, uint8_t cmd, const uint8_t *payload, size_t payload_len)
{
    uint8_t out[2 + MAX_FRAME_BYTES * 2 + 2];
    size_t out_len = 0;
    uint8_t checksum;

    if (payload_len > MAX_FRAME_BYTES)
        return -1;

    out[out_len++] = ESCAPE;
    out[out_len++] = cmd;
    for (size_t i = 0; i < payload_len; i++) {
        out[out_len++] = payload[i];
        if (payload[i] == ESCAPE && (i + 1 == payload_len || payload[i + 1] > 0x3fu))
            out[out_len++] = 0x40;
    }
    checksum = frame_checksum(payload, payload_len);
    out[out_len++] = ESCAPE;
    out[out_len++] = checksum;

    return write_all(fd, out, out_len);
}

static int parse_raw_size(const char *arg, unsigned int *width, unsigned int *height)
{
    char *x;
    char *end;
    unsigned long w;
    unsigned long h;

    x = strchr(arg, 'x');
    if (!x)
        x = strchr(arg, 'X');
    if (!x)
        return -1;

    errno = 0;
    w = strtoul(arg, &end, 0);
    if (errno || end != x || w == 0 || w > 65535)
        return -1;

    errno = 0;
    h = strtoul(x + 1, &end, 0);
    if (errno || *end != '\0' || h == 0 || h > 65535)
        return -1;

    *width = (unsigned int)w;
    *height = (unsigned int)h;
    return 0;
}

static uint32_t pack_fb_color(const struct framebuffer *fb, uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xff;
    uint32_t g = (rgb >> 8) & 0xff;
    uint32_t b = rgb & 0xff;
    uint32_t out = 0;

    if (fb->var.red.length)
        out |= (r >> (8 - fb->var.red.length)) << fb->var.red.offset;
    if (fb->var.green.length)
        out |= (g >> (8 - fb->var.green.length)) << fb->var.green.offset;
    if (fb->var.blue.length)
        out |= (b >> (8 - fb->var.blue.length)) << fb->var.blue.offset;

    return out;
}

static void fb_put_pixel(struct framebuffer *fb, int x, int y, uint32_t rgb)
{
    uint8_t *dst;
    uint32_t packed;

    if (x < 0 || y < 0 || x >= (int)fb->var.xres || y >= (int)fb->var.yres)
        return;

    dst = fb->ptr + (size_t)y * fb->fix.line_length + (size_t)x * fb->bytes_per_pixel;
    packed = pack_fb_color(fb, rgb);

    switch (fb->bytes_per_pixel) {
    case 2:
        *(uint16_t *)dst = (uint16_t)packed;
        break;
    case 3:
        dst[0] = packed & 0xff;
        dst[1] = (packed >> 8) & 0xff;
        dst[2] = (packed >> 16) & 0xff;
        break;
    case 4:
        *(uint32_t *)dst = packed;
        break;
    default:
        break;
    }
}

static void fb_fill_rect(struct framebuffer *fb, int x, int y, int w, int h, uint32_t rgb)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++)
            fb_put_pixel(fb, xx, yy, rgb);
    }
}

static void fb_clear(struct framebuffer *fb)
{
    fb_fill_rect(fb, 0, 0, (int)fb->var.xres, (int)fb->var.yres, 0x000000);
}

static void draw_dot(struct framebuffer *fb, int cx, int cy, unsigned int radius, uint32_t color)
{
    int r = (int)radius;
    int rr = r * r;

    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= rr)
                fb_put_pixel(fb, x, y, color);
        }
    }
}

static void draw_line(struct framebuffer *fb, int x0, int y0, int x1, int y1,
                      unsigned int radius, uint32_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        draw_dot(fb, x0, y0, radius, color);
        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int fb_open(struct framebuffer *fb, const char *path)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = open(path, O_RDWR);
    if (fb->fd < 0)
        return -1;

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0)
        return -1;
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) < 0)
        return -1;

    fb->bytes_per_pixel = fb->var.bits_per_pixel / 8;
    if (fb->bytes_per_pixel != 2 && fb->bytes_per_pixel != 3 && fb->bytes_per_pixel != 4) {
        errno = EINVAL;
        return -1;
    }

    fb->size = (size_t)fb->fix.line_length * fb->var.yres;
    fb->ptr = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->ptr == MAP_FAILED) {
        fb->ptr = NULL;
        return -1;
    }

    return 0;
}

static void fb_close(struct framebuffer *fb)
{
    if (fb->ptr)
        munmap(fb->ptr, fb->size);
    if (fb->fd >= 0)
        close(fb->fd);
}

static int serial_open_pnlc(const char *path)
{
    struct termios tio;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC);
    if (fd < 0)
        return -1;

    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static unsigned long long mono_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL +
           (unsigned long long)ts.tv_nsec / 1000000ULL;
}

static int send_touch_enable(int fd)
{
    const uint8_t touch_on[] = { 1 };

    return send_pnlc_frame(fd, 'I', touch_on, sizeof(touch_on));
}



static void map_touch(const struct app_state *app, int raw_x, int raw_y, int *x, int *y)
{
    // Apply calibration coefficients derived from logic analyzer corner captures
    double sx = 1.152 * raw_x - 0.145 * raw_y;
    double sy = -0.044 * raw_x + 1.112 * raw_y;

    int ix = (int)(sx + 0.5);
    int iy = (int)(sy + 0.5);

    // Clamp coordinates to screen boundaries
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= (int)app->fb.var.xres) ix = (int)app->fb.var.xres - 1;
    if (iy >= (int)app->fb.var.yres) iy = (int)app->fb.var.yres - 1;

    *x = ix;
    *y = iy;
}

static void flush_touch_decoder(struct touch_decoder *decoder, struct app_state *app) {
    if (!decoder->in_frame)
        return;

    app->frames_seen++;

    uint8_t type = decoder->type;
    const uint8_t *payload = decoder->buf;
    size_t len = decoder->len;

    if (type == 0x41 || type == 0x81) {
        if (len >= 3) {
            uint8_t A = payload[0];
            uint8_t B = payload[1];
            uint8_t C = payload[2];
            uint16_t raw_x = ((uint16_t)A << 4) | (B >> 4);
            uint16_t raw_y = ((uint16_t)(B & 0x0F) << 8) | C;

            int x, y;
            map_touch(app, raw_x, raw_y, &x, &y);
            app->touch_events++;

            if (app->touching) {
                draw_line(&app->fb, app->last_x, app->last_y, x, y, app->brush_radius, app->color);
            } else {
                draw_dot(&app->fb, x, y, app->brush_radius, app->color);
            }

            app->touching = true;
            app->last_x = x;
            app->last_y = y;
        }
    } else if (type == 0x01) {
        app->touching = false;
        app->touch_events++;
    }

    decoder->in_frame = 0;
    decoder->len = 0;
}

static void touch_decoder_feed_byte(struct touch_decoder *decoder, struct app_state *app, uint8_t byte)
{
    if (decoder->pending_star) {
        if (is_touchscreen_type(byte)) {
            flush_touch_decoder(decoder, app);
            decoder->in_frame = 1;
            decoder->type = byte;
            decoder->len = 0;
            decoder->pending_star = 0;
            return;
        } else {
            // Treat the 0x2A as part of the current payload (if any) or ignore
            if (decoder->in_frame) {
                if (decoder->len + 2 <= sizeof(decoder->buf)) {
                    decoder->buf[decoder->len++] = 0x2A;
                    decoder->buf[decoder->len++] = byte;
                }
            }
            decoder->pending_star = 0;
            return;
        }
    }

    if (byte == 0x2A) {
        decoder->pending_star = 1;
        return;
    }

    if (decoder->in_frame) {
        if (decoder->len < sizeof(decoder->buf)) {
            decoder->buf[decoder->len++] = byte;
        }
    }
}

static void stock_touch_decoder_reset(struct stock_touch_decoder *decoder)
{
    memset(decoder, 0, sizeof(*decoder));
}

static void handle_stock_touch_frame(struct app_state *app, uint8_t cmd,
                                     const uint8_t *payload, size_t len, uint8_t check)
{
    if (check != frame_checksum(payload, len))
        return;

    app->frames_seen++;

    if (cmd == 'p' && len == 5) {
        uint8_t phase = payload[0] & 0xf0u;
        uint16_t raw_x = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
        uint16_t raw_y = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
        int x, y;

        if (phase == 0x00) {
            app->touching = false;
            app->touch_events++;
            return;
        }

        map_touch(app, raw_x, raw_y, &x, &y);
        app->touch_events++;

        if (app->touching)
            draw_line(&app->fb, app->last_x, app->last_y, x, y, app->brush_radius, app->color);
        else
            draw_dot(&app->fb, x, y, app->brush_radius, app->color);

        app->touching = true;
        app->last_x = x;
        app->last_y = y;
    } else if (cmd == 't' && len == 2) {
        if (payload[1] == 0)
            app->touching = false;
        app->touch_events++;
    }
}

static void stock_touch_decoder_feed_byte(struct stock_touch_decoder *decoder,
                                          struct app_state *app, uint8_t byte)
{
    if (!decoder->in_frame) {
        if (byte == 0x2A) {
            decoder->in_frame = true;
            decoder->after_star = true;
            decoder->have_cmd = false;
            decoder->len = 0;
        }
        return;
    }

    if (decoder->after_star) {
        decoder->after_star = false;
        if (byte == 0x2A) {
            decoder->have_cmd = false;
            decoder->len = 0;
            decoder->after_star = true;
        } else if (byte == 0x40) {
            if (decoder->len < sizeof(decoder->payload))
                decoder->payload[decoder->len++] = 0x2A;
        } else if (byte & 0x80) {
            if (decoder->have_cmd)
                handle_stock_touch_frame(app, decoder->cmd, decoder->payload,
                                         decoder->len, byte);
            stock_touch_decoder_reset(decoder);
        } else if (!decoder->have_cmd) {
            decoder->cmd = byte;
            decoder->have_cmd = true;
        } else if (decoder->len + 2 <= sizeof(decoder->payload)) {
            decoder->payload[decoder->len++] = 0x2A;
            decoder->payload[decoder->len++] = byte;
        }
        return;
    }

    if (byte == 0x2A) {
        decoder->after_star = true;
    } else if (!decoder->have_cmd) {
        decoder->cmd = byte;
        decoder->have_cmd = true;
    } else if (decoder->len < sizeof(decoder->payload)) {
        decoder->payload[decoder->len++] = byte;
    }
}

static void handle_stdin_key(struct app_state *app, int key)
{
    switch (key) {
    case 'c':
    case 'C':
        fb_clear(&app->fb);
        app->touching = false;
        printf("cleared framebuffer\n");
        fflush(stdout);
        break;
    case 'q':
    case 'Q':
        running = 0;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *fb_path = DEFAULT_FB_PATH;
    const char *touch_path = DEFAULT_TOUCH_PATH;
    struct touch_decoder decoder = { 0 };
    struct stock_touch_decoder stock_decoder = { 0 };
    struct app_state app = {
        .fb = { .fd = -1 },
        .brush_radius = 6,
        .color = 0xffffff,
    };
    bool raw_size_set = false;
    unsigned long long next_touch_enable;
    int touch_fd;
    int opt;
    static const struct option long_options[] = {
        { "fb", required_argument, NULL, 'f' },
        { "touch", required_argument, NULL, 't' },
        { "raw-size", required_argument, NULL, 1000 },
        { "brush", required_argument, NULL, 1001 },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    while ((opt = getopt_long(argc, argv, "hf:t:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            fb_path = optarg;
            break;
        case 't':
            touch_path = optarg;
            break;
        case 1000:
            if (parse_raw_size(optarg, &app.raw_width, &app.raw_height) != 0) {
                fprintf(stderr, "bad --raw-size '%s' (expected WxH)\n", optarg);
                return 2;
            }
            raw_size_set = true;
            break;
        case 1001: {
            char *end;
            unsigned long radius = strtoul(optarg, &end, 0);
            if (*end || radius == 0 || radius > 128) {
                fprintf(stderr, "bad --brush '%s'\n", optarg);
                return 2;
            }
            app.brush_radius = (unsigned int)radius;
            break;
        }
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (fb_open(&app.fb, fb_path) != 0) {
        fprintf(stderr, "open framebuffer %s: %s\n", fb_path, strerror(errno));
        fb_close(&app.fb);
        return 1;
    }
    if (!raw_size_set) {
        app.raw_width = app.fb.var.xres;
        app.raw_height = app.fb.var.yres;
    }

    touch_fd = serial_open_pnlc(touch_path);
    if (touch_fd < 0) {
        fprintf(stderr, "open touch UART %s: %s\n", touch_path, strerror(errno));
        fb_close(&app.fb);
        return 1;
    }

    if (send_touch_enable(touch_fd) != 0) {
        fprintf(stderr, "enable PNLC touch reports: %s\n", strerror(errno));
        close(touch_fd);
        fb_close(&app.fb);
        return 1;
    }
    {
        usleep(100000);
        tcflush(touch_fd, TCIFLUSH);
    }
    next_touch_enable = mono_ms() + 3000ULL;

    setup_stdin_raw();
    fb_clear(&app.fb);
    touch_decoder_reset(&decoder);
    stock_touch_decoder_reset(&stock_decoder);

    printf("wing-draw: framebuffer %ux%u %ubpp, touch %s @ 115200 8N1 raw %ux%u\n",
           app.fb.var.xres, app.fb.var.yres, app.fb.var.bits_per_pixel,
           touch_path, app.raw_width, app.raw_height);
    printf("wing-draw: PNLC touch mode enabled; black canvas, white brush; serial keys: c=clear, q=quit\n");
    fflush(stdout);

    while (running) {
        struct pollfd pfds[2] = {
            { .fd = touch_fd, .events = POLLIN },
            { .fd = STDIN_FILENO, .events = POLLIN },
        };
        int ret = poll(pfds, 2, 250);

        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            uint8_t buf[128];
            ssize_t n;

            while ((n = read(touch_fd, buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    touch_decoder_feed_byte(&decoder, &app, buf[i]);
                    stock_touch_decoder_feed_byte(&stock_decoder, &app, buf[i]);
                }
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "read %s: %s\n", touch_path, strerror(errno));
                break;
            }
        }

        if (pfds[1].revents & POLLIN) {
            uint8_t keys[32];
            ssize_t n = read(STDIN_FILENO, keys, sizeof(keys));
            if (n > 0) {
                for (ssize_t i = 0; i < n; i++)
                    handle_stdin_key(&app, keys[i]);
            }
        }

        if (mono_ms() >= next_touch_enable) {
            if (send_touch_enable(touch_fd) != 0) {
                fprintf(stderr, "enable PNLC touch reports: %s\n", strerror(errno));
                break;
            }
            next_touch_enable = mono_ms() + 3000ULL;
        }
    }

    printf("wing-draw: exiting, frames=%lu touch-events=%lu\n",
           app.frames_seen, app.touch_events);
    close(touch_fd);
    fb_close(&app.fb);
    return 0;
}
