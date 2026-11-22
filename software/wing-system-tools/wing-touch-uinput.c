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
#define MAX_FRAME_BYTES 514

/* PNLC protocol and affine calibration from origin/qt-demo (85250d2). */
struct touch_decoder {
    uint8_t raw[3];
    size_t raw_len;
    bool raw_point;
    bool pending_star;
};

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
static struct {
    unsigned long long bytes, points, releases, bad_checksums;
    int raw_x, raw_y, x, y;
    bool down, write_failed;
} stats;

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

        if (n <= 0) {
            if (n == 0)
                errno = EIO;
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
        int err = errno;
        close(fd);
        errno = err;
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
        int err = errno;
        close(fd);
        errno = err;
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
    if (!stats.write_failed && write_all(fd, (const uint8_t *)&ie, sizeof(ie)) != 0) {
        perror("uinput event write");
        stats.write_failed = true;
    }
}

static void handle_touch_point(int uinput_fd, int raw_x, int raw_y)
{
    double sx = default_calibration.x_coeff[0] * raw_x + default_calibration.x_coeff[1] * raw_y + default_calibration.x_coeff[2];
    double sy = default_calibration.y_coeff[0] * raw_x + default_calibration.y_coeff[1] * raw_y + default_calibration.y_coeff[2];
    int x = clamp_to_range((int)(sx + 0.5), 1280);
    int y = clamp_to_range((int)(sy + 0.5), 800);

    emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 1);
    emit_event(uinput_fd, EV_ABS, ABS_X, x);
    emit_event(uinput_fd, EV_ABS, ABS_Y, y);
    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
    if (!stats.write_failed) {
        stats.raw_x = raw_x;
        stats.raw_y = raw_y;
        stats.x = x;
        stats.y = y;
        stats.points++;
        stats.down = true;
    }
}

static void handle_touch_release(int uinput_fd)
{
    emit_event(uinput_fd, EV_KEY, BTN_TOUCH, 0);
    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
    if (!stats.write_failed) {
        stats.releases++;
        stats.down = false;
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
    if (check != frame_checksum(payload, len)) {
        stats.bad_checksums++;
        return;
    }

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
                              int uinput_fd, uint8_t byte)
{
    if (stock_decoder->in_frame) {
        stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, byte);
        return;
    }

    if (raw_decoder->raw_point) {
        /* A packed point is exactly three bytes, including literal 0x2a. */
        uint8_t *p = raw_decoder->raw;
        p[raw_decoder->raw_len++] = byte;
        if (raw_decoder->raw_len == sizeof(raw_decoder->raw)) {
            handle_touch_point(uinput_fd, (p[0] << 4) | (p[1] >> 4),
                               ((p[1] & 0x0f) << 8) | p[2]);
            raw_decoder->raw_point = false;
        }
        return;
    }

    if (raw_decoder->pending_star) {
        if (byte == ESCAPE)
            return;
        raw_decoder->pending_star = false;
        if (byte == 0x41 || byte == 0x81) {
            raw_decoder->raw_point = true;
            raw_decoder->raw_len = 0;
        } else if (byte == 0x01) {
            /* A release must not wait for the next contact/frame. */
            handle_touch_release(uinput_fd);
        } else if (byte != 0xe1 && is_wing_frame_cmd(byte)) {
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, ESCAPE);
            stock_touch_decoder_feed_byte(stock_decoder, uinput_fd, byte);
        }
        return;
    }

    raw_decoder->pending_star = byte == ESCAPE;
}

static int report_status(const char *path, const char *state, const char *uart)
{
    if (!path)
        return 0;
    char tmp[4096];
    if (snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    fprintf(f, "state=%s\npid=%ld\nuart=%s\nserial=115200-8N1\n"
            "orientation=qt-demo-affine-1280x800\nbytes=%llu\nbad_checksums=%llu\n"
            "points=%llu\nreleases=%llu\ndown=%d\nlast_raw=%d,%d\nlast_mapped=%d,%d\n",
            state, (long)getpid(), uart, stats.bytes, stats.bad_checksums,
            stats.points, stats.releases, stats.down, stats.raw_x, stats.raw_y, stats.x, stats.y);
    if (fclose(f) != 0)
        return -1;
    return rename(tmp, path);
}

int main(int argc, char **argv)
{
    const char *touch_path = DEFAULT_TOUCH_PATH;
    const char *status_path = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "t:s:h")) != -1) {
        switch (opt) {
        case 't':
            touch_path = optarg;
            break;
        case 's':
            status_path = optarg;
            break;
        case 'h':
        default:
            fprintf(stderr, "Usage: %s [-t touch_uart_path] [-s status_file]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int touch_fd = serial_open_pnlc(touch_path);
    if (touch_fd < 0) {
        fprintf(stderr, "Failed to open touch UART %s: %s\n", touch_path, strerror(errno));
        if (report_status(status_path, "uart-unavailable", touch_path) != 0)
            perror("touch status");
        return 1;
    }

    int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        fprintf(stderr, "Failed to open /dev/uinput: %s\n", strerror(errno));
        if (report_status(status_path, "uinput-unavailable", touch_path) != 0)
            perror("touch status");
        close(touch_fd);
        return 1;
    }

    if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH) < 0 ||
        ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS) < 0 ||
        ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X) < 0 ||
        ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y) < 0 ||
        ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0) {
        perror("uinput capabilities");
        close(uinput_fd);
        close(touch_fd);
        return 1;
    }

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

    if (write_all(uinput_fd, (const uint8_t *)&uud, sizeof(uud)) != 0) {
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

    struct touch_decoder raw_decoder = { 0 };
    struct stock_touch_decoder stock_decoder = { 0 };
    unsigned long long next_touch_enable = mono_ms() + 3000ULL;
    unsigned long long next_status = 0;

    printf("wing-touch-uinput: running, mapping %s -> /dev/uinput\n", touch_path);
    fflush(stdout);

    while (running && !stats.write_failed) {
        if (mono_ms() >= next_status) {
            const char *state = stats.points || stats.releases ? "touch-events-seen" :
                                stats.bytes ? "uart-data-no-touch" : "no-uart-data";
            if (report_status(status_path, state, touch_path) != 0) {
                perror("touch status");
                status_path = NULL;
            }
            next_status = mono_ms() + 1000ULL;
        }
        struct pollfd pfd = { .fd = touch_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 250);

        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "Touch UART disconnected (poll flags %#x)\n", pfd.revents);
            break;
        }

        if (pfd.revents & POLLIN) {
            uint8_t buf[128];
            ssize_t n;

            n = read(touch_fd, buf, sizeof(buf));
            if (n > 0) {
                stats.bytes += (unsigned long long)n;
                for (ssize_t i = 0; i < n; i++) {
                    feed_touch_stream(&raw_decoder, &stock_decoder, uinput_fd, buf[i]);
                }
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
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

    if (stats.down && !stats.write_failed)
        handle_touch_release(uinput_fd);
    if (report_status(status_path, running ? "bridge-failed" : "stopped", touch_path) != 0)
        perror("touch status");
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    close(touch_fd);
    return running ? 1 : 0;
}
