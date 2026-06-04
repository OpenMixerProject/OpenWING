#include <errno.h>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wing_control_names.h"
#include "wing_surface_common.h"
#include "touchscreen_parser.h"

#define MAX_LOG_LINES 512
#define LOG_LINE_LEN 256
#define INPUT_LEN 256

static volatile sig_atomic_t g_stop;

struct app {
    struct wing_surface surface;
    struct wing_frame_parser csc_parser;
    struct wing_frame_parser pnlc_parser;
    uint8_t pnlc_raw_buf[8];
    uint8_t pnlc_raw_pe[8];
    int pnlc_raw_output;
    size_t pnlc_raw_len;
    unsigned long long pnlc_raw_pending_ms;
    struct touch_decoder touch_parser;
    uint8_t unknown_buf[256];
    size_t unknown_len;
    unsigned long long pnlc_last_rx_ms;
    char last_pldc_button[32];
    char left_log[128][LOG_LINE_LEN];
    unsigned int left_count;
    char right_log[MAX_LOG_LINES][LOG_LINE_LEN];
    unsigned int right_count;
    char input[INPUT_LEN];
    size_t input_len;
    WINDOW *left;
    WINDOW *right;
};

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
}

static unsigned long long monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (unsigned long long)ts.tv_sec * 1000ull +
           (unsigned long long)ts.tv_nsec / 1000000ull;
}

static void push_line(char lines[][LOG_LINE_LEN], unsigned int *count, unsigned int max,
                      const char *line)
{
    if (*count < max) {
        snprintf(lines[*count], LOG_LINE_LEN, "%s", line);
        ++*count;
        return;
    }
    memmove(lines[0], lines[1], (max - 1) * LOG_LINE_LEN);
    snprintf(lines[max - 1], LOG_LINE_LEN, "%s", line);
}

static void left_log(struct app *app, const char *line)
{
    push_line(app->left_log, &app->left_count, 128, line);
}

static void right_log(struct app *app, const char *line)
{
    push_line(app->right_log, &app->right_count, MAX_LOG_LINES, line);
}

static void frame_cb(void *user, const char *source, uint8_t cmd, const uint8_t *payload,
                     size_t len, uint8_t check)
{
    struct app *app = user;
    char line[LOG_LINE_LEN];

    wing_describe_frame(line, sizeof(line), source, cmd, payload, len, check);
    right_log(app, line);
}

struct pldc_report {
    uint8_t selector;
    uint8_t state_raw;
    uint8_t state_base;
    uint8_t state_bit;
    uint8_t qualifier;
    int pressed;
    int direct_button;
};

struct pldc_control_map {
    uint8_t selector;
    uint8_t state_base;
    uint8_t state_bit;
    uint8_t qualifier;
    const char *name;
};

#define PLDC_QUAL_NONE 0xffu

struct pldc_encoder_map {
    uint8_t selector;
    uint8_t qualifier;
    const char *name;
};

static const char *pldc_button_name(uint8_t id)
{
    switch (id) {
        case 0x00: return "HOME";
        case 0x01: return "EFFECTS";
        case 0x02: return "METERS";
        case 0x03: return "ROUTING";
        case 0x04: return "SETUP";
        case 0x05: return "LIBRARY";
        case 0x06: return "UTILITY";
        case 0x07: return "SELECT";
        case 0x08: return "CLR_SOLO";
        default: return NULL;
    }
}

static const struct pldc_control_map pldc_controls[] = {
    {0x2c, 0x00, 0x01, PLDC_QUAL_NONE, "HOME"},
    {0x2c, 0x00, 0x01, 0x78, "METERS"},
    {0xac, 0x00, 0x01, PLDC_QUAL_NONE, "EFFECTS"},
    {0xac, 0x00, 0x01, 0x78, "ROUTING"},
    {0x2c, 0x10, 0x40, 0x78, "SETUP"},
    {0x2c, 0x10, 0x40, 0x61, "UTILITY"},
    {0xac, 0x10, 0x40, 0x78, "LIBRARY"},
    {0xac, 0x10, 0x40, 0x71, "SELECT"},
    {0xac, 0x10, 0x40, 0x61, "SELECT"},
    {0x2c, 0x08, 0x20, 0x71, "CLR_SOLO"},
    {0x97, 0x00, 0x01, PLDC_QUAL_NONE, "ENCODER_7_TOUCH"},
    {0xd7, 0x00, 0x01, PLDC_QUAL_NONE, "ENCODER_1_TOUCH"},
    {0xd7, 0x00, 0x01, 0x78, "ENCODER_1_TOUCH"},
    {0x17, 0x00, 0x01, 0x78, "ENCODER_2_TOUCH"},
    {0x57, 0x00, 0x01, 0x78, "ENCODER_3_TOUCH"},
    {0x97, 0x00, 0x01, 0x78, "ENCODER_4_TOUCH"},
    {0x17, 0x00, 0x01, PLDC_QUAL_NONE, "ENCODER_5_TOUCH"},
    {0x57, 0x00, 0x01, PLDC_QUAL_NONE, "ENCODER_6_TOUCH"},
};

static const struct pldc_encoder_map pldc_encoders[] = {
    {0x97, PLDC_QUAL_NONE, "ENCODER_7"},
    {0xd7, PLDC_QUAL_NONE, "ENCODER_1"},
    {0x17, 0x78, "ENCODER_2"},
    {0x57, 0x78, "ENCODER_3"},
    {0x97, 0x78, "ENCODER_4"},
    {0x17, PLDC_QUAL_NONE, "ENCODER_5"},
    {0x57, PLDC_QUAL_NONE, "ENCODER_6"},
};

static int pldc_decode_report(const uint8_t *buf, size_t len, struct pldc_report *report)
{
    uint8_t state;

    if (len < 4 || buf[0] != WING_FRAME_STAR)
        return -1;
    memset(report, 0, sizeof(*report));
    report->qualifier = PLDC_QUAL_NONE;

    if (buf[1] == 0x62) {
        report->selector = buf[2];
        report->state_raw = buf[3];
        report->pressed = buf[3] == 0x01;
        report->direct_button = 1;
        return 0;
    }

    if (((buf[3] & 0x7fu) != 0x25u) && !(len == 5 && buf[3] == WING_FRAME_STAR))
        return -1;

    state = buf[2];
    report->selector = buf[1];
    report->state_raw = state;
    report->qualifier = len == 5 ? (uint8_t)(buf[4] & 0x7fu) : PLDC_QUAL_NONE;

    if (state & 0x40u) {
        report->state_bit = 0x40u;
        report->state_base = state & (uint8_t)~0x40u;
        report->pressed = 1;
    } else if (state & 0x20u) {
        report->state_bit = 0x20u;
        report->state_base = state & (uint8_t)~0x20u;
        report->pressed = 1;
    } else if (state & 0x01u) {
        report->state_bit = 0x01u;
        report->state_base = state & (uint8_t)~0x01u;
        report->pressed = 1;
    } else if (state == 0x10u) {
        report->state_bit = 0x40u;
        report->state_base = 0x10u;
    } else if (state == 0x08u) {
        report->state_bit = 0x20u;
        report->state_base = 0x08u;
    } else if (state == 0x00u) {
        report->state_bit = 0x01u;
        report->state_base = 0x00u;
    } else {
        return -1;
    }

    return 0;
}

static const char *pldc_lookup_control(const struct pldc_report *report)
{
    if (report->direct_button)
        return pldc_button_name(report->selector);

    for (size_t i = 0; i < sizeof(pldc_controls) / sizeof(pldc_controls[0]); ++i) {
        const struct pldc_control_map *entry = &pldc_controls[i];
        if (entry->selector == report->selector &&
            entry->state_base == report->state_base &&
            entry->state_bit == report->state_bit &&
            entry->qualifier == report->qualifier)
            return entry->name;
    }
    for (size_t i = 0; i < sizeof(pldc_controls) / sizeof(pldc_controls[0]); ++i) {
        const struct pldc_control_map *entry = &pldc_controls[i];
        if (entry->selector == report->selector &&
            entry->state_base == report->state_base &&
            entry->state_bit == report->state_bit &&
            entry->qualifier == PLDC_QUAL_NONE)
            return entry->name;
    }
    return NULL;
}

static const char *pldc_encoder_name(uint8_t selector, uint8_t qualifier)
{
    for (size_t i = 0; i < sizeof(pldc_encoders) / sizeof(pldc_encoders[0]); ++i) {
        if (pldc_encoders[i].selector == selector && pldc_encoders[i].qualifier == qualifier)
            return pldc_encoders[i].name;
    }
    return NULL;
}

static int pldc_report_is_encoder_motion(const struct pldc_report *report)
{
    return (report->selector == 0x57 || report->selector == 0x97 ||
            report->selector == 0x17 || report->selector == 0xd7) &&
           report->state_raw != 0x00 && report->state_raw != 0x01;
}

static int pldc_report_may_continue(const uint8_t *buf, size_t len)
{
    if (len == 4 && buf[1] == 0x62)
        return 0;
    return len == 4 && (buf[3] & 0x7fu) == 0x25u;
}

static int pldc_report_shape_complete(const uint8_t *buf, size_t len)
{
    if (len < 4 || buf[0] != WING_FRAME_STAR)
        return 0;
    if (buf[1] == 0x62)
        return 1;
    if ((buf[3] & 0x7fu) == 0x25u)
        return 1;
    return len == 5 && buf[3] == WING_FRAME_STAR;
}

static void log_pnlc_raw_report(struct app *app)
{
    char line[LOG_LINE_LEN];
    struct pldc_report report;
    const char *name;
    const char *display_name;
    int pressed = 0;
    int decoded;

    name = NULL;
    display_name = NULL;
    decoded = pldc_decode_report(app->pnlc_raw_buf, app->pnlc_raw_len, &report);
    if (decoded == 0) {
        if (app->pnlc_raw_len > 1 && app->pnlc_raw_pe[1]) {
            report.qualifier = 0x78;
        }
        pressed = report.pressed;
        name = pldc_lookup_control(&report);
    }
    if (decoded == 0 && report.direct_button) {
        const char *state = report.state_raw == 0x01 ? "pressed" :
                            report.state_raw == 0x00 ? "released" : "state";
        if (name)
            snprintf(line, sizeof(line),
                     "PNLC button id=0x%02x name=%s state=0x%02x %s raw=[",
                     report.selector, name, report.state_raw, state);
        else
            snprintf(line, sizeof(line),
                     "UNKNOWN PNLC button id=0x%02x state=0x%02x raw=[",
                     report.selector, report.state_raw);
        for (size_t i = 0; i < app->pnlc_raw_len; ++i)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%02x",
                     i ? " " : "", app->pnlc_raw_buf[i]);
        strncat(line, "]", sizeof(line) - strlen(line) - 1);
        right_log(app, line);
        if (name) {
            if (pressed)
                snprintf(app->last_pldc_button, sizeof(app->last_pldc_button), "%s", name);
            else if (strcmp(app->last_pldc_button, name) == 0)
                app->last_pldc_button[0] = '\0';
        }
        app->pnlc_raw_len = 0;
        app->pnlc_raw_pending_ms = 0;
        return;
    }
    if (decoded == 0 && pldc_report_is_encoder_motion(&report)) {
        const char *encoder_name = pldc_encoder_name(report.selector, report.qualifier);
        int delta = (int)(int8_t)report.state_raw;
        if (report.qualifier == PLDC_QUAL_NONE)
            snprintf(line, sizeof(line),
                     "PNLC encoder selector=0x%02x name=%s delta=%d direction=%s qual=none raw=[",
                     report.selector, encoder_name, delta, delta > 0 ? "right" : "left");
        else
            snprintf(line, sizeof(line),
                     "PNLC encoder selector=0x%02x name=%s delta=%d direction=%s qual=0x%02x raw=[",
                     report.selector, encoder_name, delta, delta > 0 ? "right" : "left",
                     report.qualifier);
        for (size_t i = 0; i < app->pnlc_raw_len; ++i)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%02x",
                     i ? " " : "", app->pnlc_raw_buf[i]);
        strncat(line, "]", sizeof(line) - strlen(line) - 1);
        right_log(app, line);
        app->pnlc_raw_len = 0;
        app->pnlc_raw_pending_ms = 0;
        return;
    }
    display_name = name;
    if (!pressed && name) {
        if (strcmp(name, "SETUP") == 0 && strcmp(app->last_pldc_button, "UTILITY") == 0)
            display_name = app->last_pldc_button;
        else if (strcmp(name, "LIBRARY") == 0 && strcmp(app->last_pldc_button, "SELECT") == 0)
            display_name = app->last_pldc_button;
        else if (strcmp(name, "HOME") == 0 && strcmp(app->last_pldc_button, "METERS") == 0)
            display_name = app->last_pldc_button;
        else if (strcmp(name, "EFFECTS") == 0 && strcmp(app->last_pldc_button, "ROUTING") == 0)
            display_name = app->last_pldc_button;
    }

    if (name) {
        if (report.qualifier == PLDC_QUAL_NONE)
            snprintf(line, sizeof(line),
                     "PNLC raw-report selector=0x%02x base=0x%02x bit=0x%02x qual=none name=%s state=%u %s raw=[",
                     report.selector, report.state_base, report.state_bit,
                     display_name, pressed, pressed ? "pressed" : "released");
        else
            snprintf(line, sizeof(line),
                     "PNLC raw-report selector=0x%02x base=0x%02x bit=0x%02x qual=0x%02x name=%s state=%u %s raw=[",
                     report.selector, report.state_base, report.state_bit, report.qualifier,
                     display_name, pressed, pressed ? "pressed" : "released");
    } else {
        snprintf(line, sizeof(line), "UNKNOWN PNLC raw-report=[");
    }
    for (size_t i = 0; i < app->pnlc_raw_len; ++i)
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%02x",
                 i ? " " : "", app->pnlc_raw_buf[i]);
    strncat(line, "]", sizeof(line) - strlen(line) - 1);
    right_log(app, line);
    if (display_name) {
        if (pressed)
            snprintf(app->last_pldc_button, sizeof(app->last_pldc_button), "%s", display_name);
        else if (strcmp(app->last_pldc_button, display_name) == 0)
            app->last_pldc_button[0] = '\0';
    }
    app->pnlc_raw_len = 0;
    app->pnlc_raw_pending_ms = 0;
}

static int handle_pnlc_raw_report(struct app *app, uint8_t byte, int parity_error)
{
    if (app->pnlc_raw_len > 0 && pldc_report_may_continue(app->pnlc_raw_buf, app->pnlc_raw_len)) {
        if (byte != WING_FRAME_STAR) {
            if (app->pnlc_raw_len < sizeof(app->pnlc_raw_buf)) {
                app->pnlc_raw_pe[app->pnlc_raw_len] = parity_error;
                app->pnlc_raw_buf[app->pnlc_raw_len++] = byte;
            }
            log_pnlc_raw_report(app);
            return 2;
        }
        log_pnlc_raw_report(app);
        if (app->pnlc_raw_len < sizeof(app->pnlc_raw_buf)) {
            app->pnlc_raw_pe[app->pnlc_raw_len] = parity_error;
            app->pnlc_raw_buf[app->pnlc_raw_len++] = byte;
        }
        app->pnlc_raw_pending_ms = 0;
        return 0;
    }

    if (app->pnlc_raw_len == 0 && byte != WING_FRAME_STAR)
        return 0;

    if (app->pnlc_raw_len >= sizeof(app->pnlc_raw_buf))
        app->pnlc_raw_len = 0;
    app->pnlc_raw_pe[app->pnlc_raw_len] = parity_error;
    app->pnlc_raw_buf[app->pnlc_raw_len++] = byte;

    if (app->pnlc_raw_len == 1)
        return 0;

    if (app->pnlc_raw_len == 2 &&
        app->pnlc_raw_buf[1] != 0x62 &&
        app->pnlc_raw_buf[1] != 0x2c &&
        app->pnlc_raw_buf[1] != 0xac &&
        app->pnlc_raw_buf[1] != 0x17 &&
        app->pnlc_raw_buf[1] != 0x57 &&
        app->pnlc_raw_buf[1] != 0x97 &&
        app->pnlc_raw_buf[1] != 0xd7) {
        app->pnlc_raw_len = 0;
        return 0;
    }

    if (!pldc_report_shape_complete(app->pnlc_raw_buf, app->pnlc_raw_len))
        return 1;
    if (pldc_report_may_continue(app->pnlc_raw_buf, app->pnlc_raw_len)) {
        app->pnlc_raw_pending_ms = monotonic_ms();
        return 1;
    }

    log_pnlc_raw_report(app);
    return 2;
}

static inline int is_standard_raw_type(uint8_t b) {
    if (b == 0x62 || b == 0x2c || b == 0xac || b == 0x17 || b == 0x57 || b == 0x97 || b == 0xd7)
        return 1;
    if (b >= 0x20 && b < 0x7f)
        return 1;
    return 0;
}

static void flush_touchscreen(struct app *app) {
    if (!app->touch_parser.in_frame)
        return;

    char line[LOG_LINE_LEN];
    const char *type_str = "";
    int has_coords = 0;

    switch (app->touch_parser.type) {
        case 0x41:
            type_str = "TOUCH START";
            has_coords = 1;
            break;
        case 0x81:
            type_str = "TOUCH MOVE";
            has_coords = 1;
            break;
        case 0x01:
            type_str = "TOUCH RELEASE";
            break;
        case 0xE1:
            type_str = "GESTURE TERM";
            break;
        default:
            type_str = "UNKNOWN TOUCH";
            break;
    }

    snprintf(line, sizeof(line), "PNLC touchscreen type=0x%02x (%s)", app->touch_parser.type, type_str);

    if (has_coords) {
        if (app->touch_parser.len >= 3) {
            uint8_t A = app->touch_parser.buf[0];
            uint8_t B = app->touch_parser.buf[1];
            uint8_t C = app->touch_parser.buf[2];
            uint16_t x = ((uint16_t)A << 4) | (B >> 4);
            uint16_t y = ((uint16_t)(B & 0x0F) << 8) | C;
            snprintf(line + strlen(line), sizeof(line) - strlen(line),
                     " x=%u y=%u", x, y);
            
            if (app->touch_parser.len > 3) {
                strncat(line, " extra=[", sizeof(line) - strlen(line) - 1);
                for (size_t i = 3; i < app->touch_parser.len; ++i) {
                    snprintf(line + strlen(line), sizeof(line) - strlen(line),
                             "%s%02x", i > 3 ? " " : "", app->touch_parser.buf[i]);
                }
                strncat(line, "]", sizeof(line) - strlen(line) - 1);
            }
        } else {
            strncat(line, " error=incomplete", sizeof(line) - strlen(line) - 1);
        }
    } else {
        if (app->touch_parser.len > 0) {
            strncat(line, " extra=[", sizeof(line) - strlen(line) - 1);
            for (size_t i = 0; i < app->touch_parser.len; ++i) {
                snprintf(line + strlen(line), sizeof(line) - strlen(line),
                         "%s%02x", i > 0 ? " " : "", app->touch_parser.buf[i]);
            }
            strncat(line, "]", sizeof(line) - strlen(line) - 1);
        }
    }

    right_log(app, line);
    app->touch_parser.in_frame = 0;
    app->touch_parser.len = 0;
}

static void flush_unknown(struct app *app) {
    if (app->unknown_len == 0)
        return;
    char line[LOG_LINE_LEN];
    snprintf(line, sizeof(line), "UNKNOWN PNLC packet raw=[");
    for (size_t i = 0; i < app->unknown_len; ++i) {
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%02x",
                 i ? " " : "", app->unknown_buf[i]);
    }
    strncat(line, "]", sizeof(line) - strlen(line) - 1);
    right_log(app, line);
    app->unknown_len = 0;
}

static void flush_pending_pnlc_raw_report(struct app *app)
{
    if (app->pnlc_raw_len > 0 &&
        pldc_report_may_continue(app->pnlc_raw_buf, app->pnlc_raw_len) &&
        app->pnlc_raw_pending_ms > 0 &&
        monotonic_ms() - app->pnlc_raw_pending_ms >= 20ull)
        log_pnlc_raw_report(app);

    if (app->pnlc_last_rx_ms > 0 && monotonic_ms() - app->pnlc_last_rx_ms >= 50ull) {
        if (app->touch_parser.in_frame)
            flush_touchscreen(app);
        if (app->unknown_len > 0)
            flush_unknown(app);
    }
}

static void handle_rx_byte_pnlc(struct app *app, uint8_t byte, int parity_error)
{
    app->pnlc_last_rx_ms = monotonic_ms();

    // 1. If standard/raw frame is active, let them handle it.
    if (app->pnlc_parser.in_frame || app->pnlc_raw_len > 0) {
        int raw_status = handle_pnlc_raw_report(app, byte, parity_error);
        if (raw_status == 2) {
            wing_parser_reset(&app->pnlc_parser);
            return;
        }
        if (raw_status == 1)
            return;

        wing_parser_feed(&app->pnlc_parser, "PNLC", byte, frame_cb, app);
        return;
    }

    // 2. If we are waiting to see if a byte after 0x2A starts a frame
    if (app->touch_parser.pending_star) {
        if (is_touchscreen_type(byte)) {
            // Start of touchscreen report
            flush_touchscreen(app);
            flush_unknown(app);
            app->touch_parser.in_frame = 1;
            app->touch_parser.type = byte;
            app->touch_parser.len = 0;
            app->touch_parser.pending_star = 0;
            return;
        }
        if (is_standard_raw_type(byte)) {
            // Start of standard/raw frame
            flush_touchscreen(app);
            flush_unknown(app);
            app->touch_parser.pending_star = 0;
            // Now feed the 0x2A and the command byte to standard/raw parser
            int raw_status = handle_pnlc_raw_report(app, 0x2A, parity_error);
            if (raw_status == 2) {
                wing_parser_reset(&app->pnlc_parser);
                return;
            }
            // Feed command byte
            raw_status = handle_pnlc_raw_report(app, byte, parity_error);
            if (raw_status == 2) {
                wing_parser_reset(&app->pnlc_parser);
                return;
            }
            if (raw_status == 1)
                return;

            wing_parser_feed(&app->pnlc_parser, "PNLC", 0x2A, frame_cb, app);
            wing_parser_feed(&app->pnlc_parser, "PNLC", byte, frame_cb, app);
            return;
        }
        
        // Not a touchscreen type, and not a standard/raw type.
        // So the 0x2A we saw was not a valid frame start.
        if (app->touch_parser.in_frame) {
            if (app->touch_parser.len + 2 <= sizeof(app->touch_parser.buf)) {
                app->touch_parser.buf[app->touch_parser.len++] = 0x2A;
                app->touch_parser.buf[app->touch_parser.len++] = byte;
            }
            app->touch_parser.pending_star = 0;
            return;
        } else {
            // Treat as unknown packet bytes
            if (app->unknown_len + 2 <= sizeof(app->unknown_buf)) {
                app->unknown_buf[app->unknown_len++] = 0x2A;
                app->unknown_buf[app->unknown_len++] = byte;
            }
            app->touch_parser.pending_star = 0;
            return;
        }
    }

    // 3. If we see a new 0x2A
    if (byte == 0x2A) {
        app->touch_parser.pending_star = 1;
        return;
    }

    // 4. Regular bytes (pending_star == 0, byte != 0x2A)
    if (app->touch_parser.in_frame) {
        if (app->touch_parser.len < sizeof(app->touch_parser.buf)) {
            app->touch_parser.buf[app->touch_parser.len++] = byte;
        }
        return;
    }

    // Otherwise, it's unknown/fallback raw byte
    if (app->unknown_len < sizeof(app->unknown_buf)) {
        app->unknown_buf[app->unknown_len++] = byte;
    }
}

static void handle_pnlc_byte(struct app *app, uint8_t byte)
{
    handle_rx_byte_pnlc(app, byte, 0);
}

static void log_pnlc_raw_output_byte(struct app *app, uint8_t byte)
{
    char line[LOG_LINE_LEN];

    snprintf(line, sizeof(line), "PLDC raw-output [%02x]", byte);
    right_log(app, line);
}

static void handle_rx_byte(struct app *app, struct wing_frame_parser *parser,
                           const char *source, uint8_t byte)
{
    int outside_frame = !parser->in_frame && byte != WING_FRAME_STAR;

    wing_parser_feed(parser, source, byte, frame_cb, app);
    if (outside_frame) {
        char line[LOG_LINE_LEN];
        snprintf(line, sizeof(line), "UNKNOWN %s raw=[%02x]", source, byte);
        right_log(app, line);
    }
}

static void draw_pane(WINDOW *win, const char *title)
{
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " %s ", title);
}

static void draw_ui(struct app *app)
{
    int rows;
    int cols;
    int left_w;
    int right_w;
    int body_rows;
    unsigned int start;

    getmaxyx(stdscr, rows, cols);
    left_w = cols / 2;
    right_w = cols - left_w;
    if (!app->left || !app->right) {
        app->left = newwin(rows, left_w, 0, 0);
        app->right = newwin(rows, right_w, 0, left_w);
    } else {
        wresize(app->left, rows, left_w);
        wresize(app->right, rows, right_w);
        mvwin(app->right, 0, left_w);
    }

    draw_pane(app->left, "commands");
    draw_pane(app->right, "CSC / PNLC console");

    body_rows = rows - 4;
    start = app->left_count > (unsigned int)body_rows ? app->left_count - (unsigned int)body_rows : 0;
    for (int i = 0; i < body_rows && start + (unsigned int)i < app->left_count; ++i)
        mvwprintw(app->left, 1 + i, 1, "%.*s", left_w - 2, app->left_log[start + (unsigned int)i]);
    mvwhline(app->left, rows - 3, 1, ACS_HLINE, left_w - 2);
    mvwprintw(app->left, rows - 2, 1, "> %.*s", left_w - 4, app->input);

    start = app->right_count > (unsigned int)(rows - 2) ? app->right_count - (unsigned int)(rows - 2) : 0;
    for (int i = 0; i < rows - 2 && start + (unsigned int)i < app->right_count; ++i)
        mvwprintw(app->right, 1 + i, 1, "%.*s", right_w - 2, app->right_log[start + (unsigned int)i]);

    wnoutrefresh(app->left);
    wnoutrefresh(app->right);
    doupdate();
}

static void usage_line(struct app *app)
{
    left_log(app, "commands:");
    left_log(app, "  fader <1-13|master> <0-4095|0-100%>");
    left_log(app, "  led                    list LED names");
    left_log(app, "  led <name> <0|1>       e.g. led fader_1_select 1");
    left_log(app, "  led <name> <color> <0|1>  e.g. led home white 1");
    left_log(app, "  csclights [value]      enable CSC LEDs, default 0x602f");
    left_log(app, "  cscleds <0|1>          turn all CSC LEDs on or off");
    left_log(app, "  cscbrightness <led> <lamp> <glow> <patch>  set csc brightness (led: 0-15, others: 0|1)");
    left_log(app, "  cscbrightness <led|lamp|glow|patch> <val>  set individual csc brightness");
    left_log(app, "  csclatch <port> <value> <mask>");
    left_log(app, "  backlight              enable LCD backlight");
    left_log(app, "  touch                  enable touchscreen");
    left_log(app, "  raw csc|pnlc <cmd> [hex-byte ...]");
    left_log(app, "  raw-output [on|off]    show raw PLDC bytes");
    left_log(app, "  help, quit");
}

static void list_leds(struct app *app)
{
    left_log(app, "CSC strip LEDs:");
    for (unsigned int i = 1; i <= 12; ++i) {
        char line[96];
        snprintf(line, sizeof(line), "  fader_%u_select  fader_%u_solo  fader_%u_mute", i, i, i);
        left_log(app, line);
    }
    left_log(app, "CSC layer/control LEDs:");
    left_log(app, "  1-12  13-24  25-36  37-40_aux_in  bus_master");
    left_log(app, "  main_matrix  dca  user_1  user_2");
    left_log(app, "  custom_button_1 ... custom_button_16");
    left_log(app, "  talk_a  talk_b  dim  mono");
    left_log(app, "PNLC/PLDC LEDs:");
    left_log(app, "  home white|green");
    left_log(app, "  effects white|orange");
    left_log(app, "  meters white|orange");
    left_log(app, "  routing white|orange");
    left_log(app, "  setup white|orange");
    left_log(app, "  library white|orange");
    left_log(app, "  utility white|orange");
    left_log(app, "  select white");
    left_log(app, "  clr_solo white|yellow");
}

static int parse_fader(const char *s, unsigned int *out)
{
    if (strcmp(s, "master") == 0) {
        *out = 13;
        return 0;
    }
    return wing_parse_uint(s, 13, out) == 0 && *out >= 1 ? 0 : -1;
}

static int parse_position(const char *s, unsigned int *out)
{
    size_t len = strlen(s);
    unsigned int pct;

    if (len > 1 && s[len - 1] == '%') {
        char buf[16];
        if (len >= sizeof(buf))
            return -1;
        memcpy(buf, s, len - 1);
        buf[len - 1] = '\0';
        if (wing_parse_uint(buf, 100, &pct) != 0)
            return -1;
        *out = (pct * 4095u + 50u) / 100u;
        return 0;
    }
    return wing_parse_uint(s, 4095, out);
}

static void handle_command(struct app *app, char *line)
{
    char original[INPUT_LEN];
    char *cmd;

    snprintf(original, sizeof(original), "> %s", line);
    left_log(app, original);
    cmd = strtok(line, " \t\r\n");
    if (!cmd)
        return;
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        g_stop = 1;
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        usage_line(app);
        return;
    }
    if (strcmp(cmd, "fader") == 0) {
        char *fader_s = strtok(NULL, " \t\r\n");
        char *value_s = strtok(NULL, " \t\r\n");
        unsigned int fader;
        unsigned int value;
        if (!fader_s || !value_s || parse_fader(fader_s, &fader) != 0 ||
            parse_position(value_s, &value) != 0) {
            left_log(app, "usage: fader <1-13|master> <0-4095|0-100%>");
            return;
        }
        if (wing_surface_fader(&app->surface, fader, value) != 0)
            left_log(app, "fader command failed");
        else {
            char msg[80];
            snprintf(msg, sizeof(msg), "sent fader %u value %u", fader, value);
            left_log(app, msg);
        }
        return;
    }
    if (strcmp(cmd, "led") == 0) {
        char *name = strtok(NULL, " \t\r\n");
        char *arg2 = strtok(NULL, " \t\r\n");
        char *arg3 = strtok(NULL, " \t\r\n");
        const char *color = NULL;
        const char *state_s = arg2;
        unsigned int state;
        if (!name) {
            list_leds(app);
            return;
        }
        if (arg3) {
            color = arg2;
            state_s = arg3;
        }
        if (!state_s || wing_parse_uint(state_s, 1, &state) != 0) {
            left_log(app, "usage: led <name> [color] <0|1>");
            return;
        }
        if (wing_surface_led_color(&app->surface, name, color, state != 0) != 0)
            left_log(app, "led command failed or unknown name/color");
        else
            left_log(app, "sent led state");
        return;
    }
    if (strcmp(cmd, "csclights") == 0) {
        char *value_s = strtok(NULL, " \t\r\n");
        unsigned int value = 0x602fu;
        if (value_s && wing_parse_uint(value_s, 0x602fu, &value) != 0) {
            left_log(app, "usage: csclights [0-0x602f]");
            return;
        }
        if (wing_surface_csc_lights(&app->surface, value) != 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "CSC light enable failed: %s", strerror(errno));
            left_log(app, msg);
        } else {
            char msg[96];
            snprintf(msg, sizeof(msg), "sent CSC light value 0x%04x", value);
            left_log(app, msg);
        }
        return;
    }
    if (strcmp(cmd, "cscleds") == 0) {
        char *state_s = strtok(NULL, " \t\r\n");
        unsigned int state;
        if (!state_s || wing_parse_uint(state_s, 1, &state) != 0) {
            left_log(app, "usage: cscleds <0|1>");
            return;
        }
        if (wing_surface_csc_leds_all(&app->surface, state != 0) != 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "cscleds command failed: %s", strerror(errno));
            left_log(app, msg);
        } else {
            left_log(app, state ? "all CSC LEDs turned ON" : "all CSC LEDs turned OFF");
        }
        return;
    }
    if (strcmp(cmd, "cscbrightness") == 0) {
        char *arg1 = strtok(NULL, " \t\r\n");
        if (!arg1) {
            left_log(app, "usage: cscbrightness <led> <lamp> <glow> <patch> OR cscbrightness <led|lamp|glow|patch> <val>");
            return;
        }

        if (strcmp(arg1, "led") == 0 || strcmp(arg1, "lamp") == 0 ||
            strcmp(arg1, "glow") == 0 || strcmp(arg1, "patch") == 0) {
            char *val_s = strtok(NULL, " \t\r\n");
            unsigned int val;
            if (!val_s || wing_parse_uint(val_s, 15, &val) != 0) {
                left_log(app, "usage: cscbrightness <led|lamp|glow|patch> <val>");
                return;
            }
            int led = -1, lamp = -1, glow = -1, patch = -1;
            if (strcmp(arg1, "led") == 0) led = (int)val;
            else if (strcmp(arg1, "lamp") == 0) {
                if (val > 1) { left_log(app, "lamp must be 0 or 1"); return; }
                lamp = (int)val;
            } else if (strcmp(arg1, "glow") == 0) {
                if (val > 1) { left_log(app, "glow must be 0 or 1"); return; }
                glow = (int)val;
            } else if (strcmp(arg1, "patch") == 0) {
                if (val > 1) { left_log(app, "patch must be 0 or 1"); return; }
                patch = (int)val;
            }
            if (wing_surface_csc_brightness(&app->surface, led, lamp, glow, patch) != 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "CSC brightness update failed: %s", strerror(errno));
                left_log(app, msg);
            } else {
                char msg[96];
                snprintf(msg, sizeof(msg), "sent CSC brightness %s=%u", arg1, val);
                left_log(app, msg);
            }
        } else {
            // Assume 4 values format: <led> <lamp> <glow> <patch>
            char *arg2 = strtok(NULL, " \t\r\n");
            char *arg3 = strtok(NULL, " \t\r\n");
            char *arg4 = strtok(NULL, " \t\r\n");
            unsigned int led, lamp, glow, patch;
            if (!arg2 || !arg3 || !arg4 ||
                wing_parse_uint(arg1, 15, &led) != 0 ||
                wing_parse_uint(arg2, 1, &lamp) != 0 ||
                wing_parse_uint(arg3, 1, &glow) != 0 ||
                wing_parse_uint(arg4, 1, &patch) != 0) {
                left_log(app, "usage: cscbrightness <led> <lamp> <glow> <patch> (led: 0-15, others: 0|1)");
                return;
            }
            if (wing_surface_csc_brightness(&app->surface, (int)led, (int)lamp, (int)glow, (int)patch) != 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "CSC brightness update failed: %s", strerror(errno));
                left_log(app, msg);
            } else {
                char msg[96];
                snprintf(msg, sizeof(msg), "sent CSC brightness led=%u lamp=%u glow=%u patch=%u", led, lamp, glow, patch);
                left_log(app, msg);
            }
        }
        return;
    }
    if (strcmp(cmd, "csclatch") == 0) {
        char *port_s = strtok(NULL, " \t\r\n");
        char *value_s = strtok(NULL, " \t\r\n");
        char *mask_s = strtok(NULL, " \t\r\n");
        unsigned int port;
        unsigned int value;
        unsigned int mask;
        if (!port_s || !value_s || !mask_s || wing_parse_uint(port_s, 1, &port) != 0 ||
            wing_parse_uint(value_s, 0xffffffffu, &value) != 0 ||
            wing_parse_uint(mask_s, 0xffffffffu, &mask) != 0) {
            left_log(app, "usage: csclatch <0|1> <value> <mask>");
            return;
        }
        if (wing_surface_csc_latch(&app->surface, port, value, mask) != 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "CSC latch update failed: %s", strerror(errno));
            left_log(app, msg);
        } else {
            left_log(app, "sent CSC latch update");
        }
        return;
    }
    if (strcmp(cmd, "backlight") == 0) {
        int rc = app->surface.pnlc_fd < 0 ? -1 : wing_enable_pnlc_backlight(app->surface.pnlc_fd);
        left_log(app, rc == 0 ? "sent backlight enable" : "backlight command failed");
        return;
    }
    if (strcmp(cmd, "touch") == 0 || strcmp(cmd, "touchscreen") == 0) {
        int rc = app->surface.pnlc_fd < 0 ? -1 : wing_enable_pnlc_touch(app->surface.pnlc_fd);
        left_log(app, rc == 0 ? "sent touchscreen enable" : "touchscreen command failed");
        return;
    }
    if (strcmp(cmd, "raw") == 0) {
        char *target = strtok(NULL, " \t\r\n");
        char *cmd_s = strtok(NULL, " \t\r\n");
        uint8_t payload[512];
        size_t len = 0;
        char *tok;
        int rc;
        if (!target || !cmd_s || strlen(cmd_s) != 1) {
            left_log(app, "usage: raw csc|pnlc <cmd> [hex-byte ...]");
            return;
        }
        while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
            if (len >= sizeof(payload) || wing_parse_hex_byte(tok, &payload[len]) != 0) {
                left_log(app, "bad raw hex byte");
                return;
            }
            ++len;
        }
        if (strcmp(target, "csc") == 0)
            rc = wing_surface_raw_csc(&app->surface, (uint8_t)cmd_s[0], payload, len);
        else if (strcmp(target, "pnlc") == 0 || strcmp(target, "plnc") == 0)
            rc = wing_surface_raw_pnlc(&app->surface, (uint8_t)cmd_s[0], payload, len);
        else {
            left_log(app, "raw target must be csc or pnlc");
            return;
        }
        left_log(app, rc == 0 ? "sent raw frame" : "raw frame failed");
        return;
    }
    if (strcmp(cmd, "raw-output") == 0) {
        char *state_s = strtok(NULL, " \t\r\n");
        if (!state_s) {
            app->pnlc_raw_output = !app->pnlc_raw_output;
        } else if (strcmp(state_s, "on") == 0 || strcmp(state_s, "1") == 0) {
            app->pnlc_raw_output = 1;
        } else if (strcmp(state_s, "off") == 0 || strcmp(state_s, "0") == 0) {
            app->pnlc_raw_output = 0;
        } else {
            left_log(app, "usage: raw-output [on|off]");
            return;
        }
        left_log(app, app->pnlc_raw_output ? "PLDC raw output enabled" :
                                             "PLDC raw output disabled");
        return;
    }
    left_log(app, "unknown command; type help");
}

int main(int argc, char **argv)
{
    const char *csc_device = argc >= 2 ? argv[1] : WING_CSC_DEVICE;
    const char *pnlc_device = argc >= 3 ? argv[2] : WING_PNLC_DEVICE;
    struct app app;
    unsigned long long next_csc_probe;
    unsigned long long next_pnlc_enable;

    memset(&app, 0, sizeof(app));
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    wing_parser_reset(&app.csc_parser);
    wing_parser_reset(&app.pnlc_parser);

    if (wing_surface_open(&app.surface, csc_device, WING_CSC_BAUD, pnlc_device, 1) != 0) {
        fprintf(stderr, "wing_surface_console: surface init failed: %s\n", strerror(errno));
        wing_surface_close(&app.surface);
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    set_escdelay(25);
    usage_line(&app);
    right_log(&app, "surface initialized");
    next_csc_probe = monotonic_ms() + 3000ull;
    next_pnlc_enable = monotonic_ms() + 3000ull;

    while (!g_stop) {
        struct pollfd pfds[2];
        nfds_t nfds = 1;
        int timeout = 50;
        int ch;
        unsigned long long now = monotonic_ms();

        if (now >= next_csc_probe) {
            (void)wing_surface_raw_csc(&app.surface, 'H', NULL, 0);
            next_csc_probe = now + 3000ull;
        }
        if (app.surface.pnlc_fd >= 0 && now >= next_pnlc_enable) {
            (void)wing_enable_pnlc_touch(app.surface.pnlc_fd);
            next_pnlc_enable = now + 3000ull;
        }

        pfds[0].fd = app.surface.csc_fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        if (app.surface.pnlc_fd >= 0) {
            pfds[1].fd = app.surface.pnlc_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }
        if (poll(pfds, nfds, timeout) > 0) {
            uint8_t buf[256];
            ssize_t n;
            if (pfds[0].revents & POLLIN) {
                while ((n = read(app.surface.csc_fd, buf, sizeof(buf))) > 0)
                    for (ssize_t i = 0; i < n; ++i)
                        handle_rx_byte(&app, &app.csc_parser, "CSC", buf[i]);
            }
            if (nfds > 1 && (pfds[1].revents & POLLIN)) {
                while ((n = read(app.surface.pnlc_fd, buf, sizeof(buf))) > 0)
                    for (ssize_t i = 0; i < n; ++i) {
                        if (app.pnlc_raw_output)
                            log_pnlc_raw_output_byte(&app, buf[i]);
                        handle_pnlc_byte(&app, buf[i]);
                    }
            }
        }
        flush_pending_pnlc_raw_report(&app);

        while ((ch = getch()) != ERR) {
            if (ch == KEY_RESIZE) {
                continue;
            } else if (ch == '\n' || ch == '\r') {
                char line[INPUT_LEN];
                snprintf(line, sizeof(line), "%s", app.input);
                app.input[0] = '\0';
                app.input_len = 0;
                handle_command(&app, line);
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (app.input_len)
                    app.input[--app.input_len] = '\0';
            } else if (ch >= 0x20 && ch < 0x7f && app.input_len + 1 < sizeof(app.input)) {
                app.input[app.input_len++] = (char)ch;
                app.input[app.input_len] = '\0';
            }
        }
        draw_ui(&app);
    }

    endwin();
    wing_surface_close(&app.surface);
    return 0;
}
