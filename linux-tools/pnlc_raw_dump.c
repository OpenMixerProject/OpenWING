/*
 * pnlc_raw_dump - Dump raw bytes from the PNLC (display STM32) UART as hex.
 *
 * Opens /dev/ttymxc3 in the rawest possible mode and prints every received
 * byte as two-digit hex to stdout.  Sends the touch-enable command ('I' {1})
 * on startup and every 3 seconds.
 *
 * Usage: pnlc_raw_dump [-p] [-d DEVICE]
 *   -p          enable even parity (default: no parity)
 *   -d DEVICE   serial device (default: /dev/ttymxc3)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>

#define WING_FRAME_STAR 0x2Au

static volatile int running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

static uint8_t wing_checksum(const uint8_t *payload, size_t len)
{
    unsigned int sum = 0;
    for (size_t i = 0; i < len; i++)
        sum = (sum + payload[i]) & 0xFFu;
    return (uint8_t)(((sum & 0xFFu) ^ (len & 0xFFu)) | 0x80u);
}

static int wing_send_frame(int fd, uint8_t cmd,
                           const uint8_t *payload, size_t len)
{
    uint8_t out[2 + 512 * 2 + 2];
    size_t n = 0;

    out[n++] = WING_FRAME_STAR;
    out[n++] = cmd;
    for (size_t i = 0; i < len; i++) {
        if (payload[i] == WING_FRAME_STAR) {
            out[n++] = WING_FRAME_STAR;
            out[n++] = 0x40;
        } else {
            out[n++] = payload[i];
        }
    }
    out[n++] = WING_FRAME_STAR;
    out[n++] = wing_checksum(payload, len);

    ssize_t w = write(fd, out, n);
    return (w == (ssize_t)n) ? 0 : -1;
}

static void send_touch_enable(int fd)
{
    uint8_t payload[1] = { 1 };
    int rc = wing_send_frame(fd, 'I', payload, sizeof(payload));
    fprintf(stderr, "[pnlc_raw_dump] sent touch enable 'I' {0x01}: %s\n",
            rc == 0 ? "ok" : "FAILED");
}

static unsigned long long mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL +
           (unsigned long long)ts.tv_nsec / 1000000ULL;
}

int main(int argc, char **argv)
{
    const char *device = "/dev/ttymxc3";
    int use_parity = 0;
    int opt;
    int fd;
    struct termios tio;
    unsigned long count = 0;
    int cols = 0;

    while ((opt = getopt(argc, argv, "pd:")) != -1) {
        switch (opt) {
        case 'p':
            use_parity = 1;
            break;
        case 'd':
            device = optarg;
            break;
        default:
            fprintf(stderr, "usage: %s [-p] [-d device]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", device, strerror(errno));
        return 1;
    }

    if (tcgetattr(fd, &tio) != 0) {
        perror("tcgetattr");
        close(fd);
        return 1;
    }

    cfmakeraw(&tio);

    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag = CS8 | CREAD | CLOCAL;

    if (use_parity) {
        tio.c_cflag |= PARENB;
        tio.c_cflag &= ~PARODD;
        fprintf(stderr, "[pnlc_raw_dump] parity: EVEN\n");
    } else {
        tio.c_cflag &= ~PARENB;
        fprintf(stderr, "[pnlc_raw_dump] parity: NONE\n");
    }

    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;

    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        close(fd);
        return 1;
    }
    tcflush(fd, TCIOFLUSH);

    fprintf(stderr, "[pnlc_raw_dump] device: %s @ 115200 8%s1\n",
            device, use_parity ? "E" : "N");
    fprintf(stderr, "[pnlc_raw_dump] dumping raw bytes as hex (Ctrl-C to stop)...\n");

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Send touch enable immediately */
    send_touch_enable(fd);
    unsigned long long next_touch = mono_ms() + 3000ULL;

    while (running) {
        unsigned char buf[256];
        fd_set rfds;
        struct timeval tv;
        int ret;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;  /* 100ms poll */

        ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (ret > 0) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    printf("%02x ", buf[i]);
                    cols++;
                    count++;
                    if (cols >= 32) {
                        printf("\n");
                        cols = 0;
                    }
                }
                fflush(stdout);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                break;
            }
        }

        /* Re-send touch enable every 3 seconds */
        if (mono_ms() >= next_touch) {
            send_touch_enable(fd);
            next_touch = mono_ms() + 3000ULL;
        }
    }

    if (cols > 0)
        printf("\n");
    fprintf(stderr, "\n[pnlc_raw_dump] %lu bytes received\n", count);

    close(fd);
    return 0;
}
