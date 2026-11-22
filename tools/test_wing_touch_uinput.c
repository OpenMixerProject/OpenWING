/* Linux: cc -Wall -Wextra -Werror tools/test_wing_touch_uinput.c -o /tmp/test-touch */
#define main bridge_main
#include "../software/wing-system-tools/wing-touch-uinput.c"
#undef main
#include <assert.h>

static void expect_event(int fd, uint16_t type, uint16_t code, int value)
{
    struct input_event ev;
    assert(read(fd, &ev, sizeof(ev)) == sizeof(ev));
    assert(ev.type == type && ev.code == code && ev.value == value);
}

static void expect_empty(int fd)
{
    struct input_event ev;
    assert(read(fd, &ev, sizeof(ev)) == -1 && errno == EAGAIN);
}

static void expect_point(int fd, int x, int y)
{
    expect_event(fd, EV_KEY, BTN_TOUCH, 1);
    expect_event(fd, EV_ABS, ABS_X, x);
    expect_event(fd, EV_ABS, ABS_Y, y);
    expect_event(fd, EV_SYN, SYN_REPORT, 0);
    expect_empty(fd);
}

static void expect_release(int fd)
{
    expect_event(fd, EV_KEY, BTN_TOUCH, 0);
    expect_event(fd, EV_SYN, SYN_REPORT, 0);
    expect_empty(fd);
}

int main(void)
{
    struct touch_decoder raw = {0};
    struct stock_touch_decoder stock = {0};
    int fds[2];
    assert(pipe2(fds, O_NONBLOCK) == 0);

    /* Synthetic wire fixtures with independently calculated event values. */
    const uint8_t packed[] = {0x2a, 0x41, 0x2a, 0x92, 0x43}; /* (681, 579) */
    for (size_t i = 0; i < sizeof(packed) - 1; ++i)
        feed_touch_stream(&raw, &stock, fds[1], packed[i]);
    expect_empty(fds[0]);
    feed_touch_stream(&raw, &stock, fds[1], packed[sizeof(packed) - 1]);
    expect_point(fds[0], 681, 573);
    feed_touch_stream(&raw, &stock, fds[1], 0x2a);
    feed_touch_stream(&raw, &stock, fds[1], 0x01);
    expect_release(fds[0]); /* No trailing byte, timeout or next contact. */

    const uint8_t bad[] = {0x2a, 'p', 0x10, 0x2a, 0x01, 0x24, 0x02, 0x2a, 0xe5};
    const uint8_t press[] = {0x2a, 'p', 0x10, 0x2a, 0x40, 0x01, 0x24, 0x02, 0x2a, 0xe4};
    const uint8_t move[] = {0x2a, 'p', 0x20, 0x2a, 0x01, 0x24, 0x02, 0x2a, 0xf4};
    const uint8_t lift[] = {0x2a, 'p', 0x00, 0x2a, 0x01, 0x24, 0x02, 0x2a, 0xd4};
    const uint8_t contact_up[] = {0x2a, 't', 0, 0, 0x2a, 0x82};
    const uint8_t clamped[] = {0x2a, 0x81, 0xff, 0xf0, 0x00}; /* (4095, 0) */
    const uint8_t clamped_y[] = {0x2a, 0x81, 0x00, 0x0f, 0xff}; /* (0, 4095) */
    const uint8_t *frames[] = {bad, press, move, lift, clamped, clamped_y, contact_up};
    const size_t lengths[] = {sizeof(bad), sizeof(press), sizeof(move), sizeof(lift),
                              sizeof(clamped), sizeof(clamped_y), sizeof(contact_up)};
    for (size_t frame = 0; frame < sizeof(frames) / sizeof(frames[0]); ++frame) {
        for (size_t i = 0; i < lengths[frame]; ++i)
            feed_touch_stream(&raw, &stock, fds[1], frames[frame][i]);
        if (frame == 0)
            expect_empty(fds[0]);
        else if (frame == 1 || frame == 2)
            expect_point(fds[0], 294, 542); /* stock little-endian (298, 548) */
        else if (frame == 4)
            expect_point(fds[0], 1279, 0);
        else if (frame == 5)
            expect_point(fds[0], 41, 799);
        else
            expect_release(fds[0]);
    }
    assert(stats.points == 5 && stats.releases == 3 && stats.bad_checksums == 1 && !stats.down);
    assert(send_touch_enable(fds[1]) == 0);
    uint8_t wire[5];
    assert(read(fds[0], wire, sizeof(wire)) == sizeof(wire));
    assert(memcmp(wire, "\x2a" "I" "\x01\x2a\x80", sizeof(wire)) == 0);
    close(fds[0]);
    close(fds[1]);
}
