#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define DEFAULT_TOUCH_PATH "/dev/ttymxc3"
#define ESCAPE 0x2a
#include "touchscreen_parser.h"

struct stock_touch_decoder {
    uint8_t payload[512];
    size_t len;
    bool in_frame;
    bool after_star;
    bool have_cmd;
    uint8_t cmd;
};

struct touch_calibration {
    double x_coeff[3];
    double y_coeff[3];
};

static const struct touch_calibration default_calibration = {
    .x_coeff = { 1.008553746, 0.013419642, -13.539 },
    .y_coeff = { -0.000442457, 0.998999220, -5.510 },
};

static volatile sig_atomic_t running = 1;

static void handle_signal(int signo)
{
    (void)signo;
    running = 0;
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

static int clamp_to_range(int value, int max)
{
    if (value < 0)
        return 0;
    if (value >= max)
        return max - 1;
    return value;
}

static void emit_event(int fd, uint16_t type, uint16_t code, int32_t val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    if (write(fd, &ie, sizeof(ie)) < 0) {
        // Ignore error or log
    }
}

static void handle_touch_point(int uinput_fd, int raw_x, int raw_y)
{
    double sx = default_calibration.x_coeff[0] * raw_x + default_calibration.x_coeff[1] * raw_y + default_calibration.x_coeff[2];
    double sy = default_calibration.y_coeff[0] * raw_x + default_calibration.y_coeff[1] * raw_y + default_calibration.y_coeff[2];
    int x = clamp_to_range((int)(sx + 0.5), 1280);
    int y = clamp_to_range((int)(sy + 0.5), 800);

    emit_event(uinput_fd, EV_ABS, ABS_X, x);
    emit_event(uinput_fd, EV_ABS, ABS_Y, y);
    emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 1);
    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

static void handle_touch_release(int uinput_fd)
{
    emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 0);
    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

static void flush_touch_decoder(struct touch_decoder *decoder, int uinput_fd)
{
    if (!decoder->in_frame)
        return;

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

            handle_touch_point(uinput_fd, raw_x, raw_y);
        }
    } else if (type == 0x01) {
        handle_touch_release(uinput_fd);
    }

    decoder->in_frame = 0;
    decoder->len = 0;
}

static void touch_decoder_feed_byte(struct touch_decoder *decoder, int uinput_fd, uint8_t byte)
{
    if (decoder->pending_star) {
        if (is_touchscreen_type(byte)) {
            flush_touch_decoder(decoder, uinput_fd);
            decoder->in_frame = 1;
            decoder->type = byte;
            decoder->len = 0;
            decoder->pending_star = 0;
            return;
        } else {
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

static int is_wing_frame_cmd(uint8_t byte)
{
    return byte >= 0x20 && byte < 0x7f;
}

static void stock_touch_decoder_reset(struct stock_touch_decoder *decoder)
{
    memset(decoder, 0, sizeof(*decoder));
}

static void handle_stock_touch_frame(int uinput_fd, uint8_t cmd,
                                     const uint8_t *payload, size_t len, uint8_t check)
{
    if (check != frame_checksum(payload, len))
        return;

    if (cmd == 'p' && len == 5) {
        uint8_t phase = payload[0] & 0xf0u;
        uint16_t raw_x = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
        uint16_t raw_y = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
        if ((payload[0] & 0x0fu) != 0)
            return;
        if (phase != 0x00 && phase != 0x10 && phase != 0x20)
            return;
        if (phase == 0x00) {
            handle_touch_release(uinput_fd);
            return;
        }

        handle_touch_point(uinput_fd, raw_x, raw_y);
    } else if (cmd == 't' && len == 2) {
        if (payload[1] != 0 && payload[1] != 1)
            return;
        if (payload[1] == 0)
            handle_touch_release(uinput_fd);
    }
}

static void stock_touch_decoder_feed_byte(struct stock_touch_decoder *decoder,
                                          int uinput_fd, uint8_t byte)
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
                handle_stock_touch_frame(uinput_fd, decoder->cmd, decoder->payload,
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

static void feed_touch_stream(struct touch_decoder *raw_decoder,
                              struct stock_touch_decoder *stock_decoder,
                              int *pending_star,
                              int uinput_fd,
                              uint8_t byte)
{
    if (stock_decoder->in_frame) {
        stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, byte);
        return;
    }

    if (raw_decoder->in_frame || raw_decoder->pending_star) {
        if (raw_decoder->pending_star &&
            !is_touchscreen_type(byte) &&
            is_wing_frame_cmd(byte)) {
            touch_decoder_reset(raw_decoder);
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, 0x2A);
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, byte);
            return;
        }
        touch_decoder_feed_byte(raw_decoder, uinput_fd, byte);
        return;
    }

    if (*pending_star) {
        if (byte == 0x2A) {
            return;
        }
        if (is_touchscreen_type(byte)) {
            touch_decoder_feed_byte(raw_decoder, uinput_fd, 0x2A);
            touch_decoder_feed_byte(raw_decoder, uinput_fd, byte);
        } else if (is_wing_frame_cmd(byte)) {
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, 0x2A);
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, byte);
        }
        *pending_star = 0;
        return;
    }

    if (byte == 0x2A)
        *pending_star = 1;
}

int main(int argc, char **argv)
{
    const char *touch_path = DEFAULT_TOUCH_PATH;
    int opt;

    while ((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
        case 't':
            touch_path = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr, "Usage: %s [-t touch_uart_path]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int touch_fd = serial_open_pnlc(touch_path);
    if (touch_fd < 0) {
        fprintf(stderr, "Failed to open touch UART %s: %s\n", touch_path, strerror(errno));
        return 1;
    }

    int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        fprintf(stderr, "Failed to open /dev/uinput: %s\n", strerror(errno));
        close(touch_fd);
        return 1;
    }

    // Configure uinput device
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);

    struct uinput_user_dev uud;
    memset(&uud, 0, sizeof(uud));
    snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "Wing Touchscreen");
    uud.id.bustype = BUS_USB;
    uud.id.vendor  = 0x3434;
    uud.id.product = 0x0103;
    uud.absmin[ABS_X] = 0;
    uud.absmax[ABS_X] = 1279;
    uud.absmin[ABS_Y] = 0;
    uud.absmax[ABS_Y] = 799;

    if (write(uinput_fd, &uud, sizeof(uud)) < 0) {
        fprintf(stderr, "Failed to write uinput device info: %s\n", strerror(errno));
        close(uinput_fd);
        close(touch_fd);
        return 1;
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "Failed to create uinput device: %s\n", strerror(errno));
        close(uinput_fd);
        close(touch_fd);
        return 1;
    }

    if (send_touch_enable(touch_fd) != 0) {
        fprintf(stderr, "Failed to enable touch reports: %s\n", strerror(errno));
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        close(touch_fd);
        return 1;
    }

    {
        usleep(100000);
        tcflush(touch_fd, TCIFLUSH);
    }

    struct touch_decoder raw_decoder = { 0 };
    struct stock_touch_decoder stock_decoder = { 0 };
    int pending_frame_star = 0;
    unsigned long long next_touch_enable = mono_ms() + 3000ULL;

    touch_decoder_reset(&raw_decoder);
    stock_touch_decoder_reset(&stock_decoder);

    printf("wing-touch-uinput: running, mapping %s -> /dev/uinput\n", touch_path);
    fflush(stdout);

    while (running) {
        struct pollfd pfd = { .fd = touch_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 250);

        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (pfd.revents & POLLIN) {
            uint8_t buf[128];
            ssize_t n;

            while ((n = read(touch_fd, buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    feed_touch_stream(&raw_decoder, &stock_decoder, &pending_frame_star,
                                      uinput_fd, buf[i]);
                }
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "read error: %s\n", strerror(errno));
                break;
            }
        }

        if (mono_ms() >= next_touch_enable) {
            if (send_touch_enable(touch_fd) != 0) {
                fprintf(stderr, "Failed to send keepalive touch enable: %s\n", strerror(errno));
                break;
            }
            next_touch_enable = mono_ms() + 3000ULL;
        }
    }

    printf("wing-touch-uinput: exiting\n");
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    close(touch_fd);
    return 0;
}
