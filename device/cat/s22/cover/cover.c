/*
 * cat_s22_cover: CAT S22 Flip cover (outer) display service.
 *
 * Draws on the 128x128 SPI panel (/dev/graphics/fb2) only while the lid is
 * closed and there is something to show:
 *   - for timeout_s after the lid closes, a new missed call or a new SMS;
 *   - for the whole time a call is ringing or active, and timeout_s after it.
 * Screens: idle (clock, date, signal, Wi-Fi, battery, missed calls, SMS),
 * incoming call (name/number), active call (name, timer).
 *
 * Inputs, all event-driven (no polling while dark):
 *   - lid: /sys/devices/virtual/switch_hall/hall_switch/state via kernel
 *     uevents (the hall_sensor input device never reports SW_LID);
 *   - CatS22Cover app: config.txt and state.txt (key=value) in its
 *     device-protected files directory, watched with inotify.
 * While lit it redraws once a second and holds a self-expiring wakelock (the
 * SPI bus is power-gated in suspend).
 *
 * Copyright 2026 CAT S22 port contributors. Apache License 2.0.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "cover_font.h"

#define W 128
#define H 128
#define FB_DEV "/dev/graphics/fb2"
#define BACKLIGHT "/sys/class/leds/lcd-backlight2/brightness"
#define LID_STATE "/sys/devices/virtual/switch_hall/hall_switch/state"
#define BATTERY "/sys/class/power_supply/battery/"
#define DEFAULT_APP_DIR "/data/user_de/0/cats22.cover/files"
static const char *app_dir = DEFAULT_APP_DIR;
#define WAKELOCK "cat_s22_cover"

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
static const uint32_t C_BG = RGB(0, 0, 0);
static const uint32_t C_TEXT = RGB(255, 255, 255);
static const uint32_t C_DIM = RGB(150, 150, 150);
static const uint32_t C_ACCENT = RGB(255, 179, 64);   /* CAT-style amber */
static const uint32_t C_GOOD = RGB(80, 200, 120);
static const uint32_t C_ALERT = RGB(240, 80, 70);

typedef struct {
    int enabled, timeout_s, bright_day, bright_night, night_start, night_end;
    int show_call, show_missed, show_sms, sms_text;
    char days[7][16], months[12][24];
    char lbl_incoming[48], lbl_call[48], lbl_unknown[48], lbl_no_service[48];
} config_t;

typedef struct {
    int call;                 /* 0 idle, 1 ringing, 2 active */
    char call_name[96], call_number[40];
    long long call_since_ms;  /* wall clock */
    int missed, sms;
    char missed_name[96], sms_name[96], sms_body[160];
    int signal, wifi;         /* -1 none, 0..4 */
    char operator_name[48];
    long long seq;            /* bumps on every new missed call / SMS */
} state_t;

static volatile sig_atomic_t stopping;
static config_t cfg;
static state_t st;
static uint32_t canvas[W * H];
static int fb_fd = -1;
static uint32_t *fb_map;
static struct fb_var_screeninfo vinfo;
static bool lit;

/* ---------- helpers ---------- */

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void note(const char *event, long long value) {
    fprintf(stderr, "cover t=%lld event=%s value=%lld\n", (long long)now_ms(), event, value);
    fflush(stderr);
}

static void write_node(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t unused = write(fd, value, strlen(value));
        (void)unused;
        close(fd);
    }
}

static int read_int(const char *path, int fallback) {
    char buf[32] = {0};
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return fallback;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return n > 0 && buf[0] >= '0' && buf[0] <= '9' ? atoi(buf) : fallback;
}

static void read_text(const char *path, char *out, size_t size) {
    out[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    ssize_t n = read(fd, out, size - 1);
    close(fd);
    out[n > 0 ? n : 0] = 0;
    char *nl = strchr(out, '\n');
    if (nl) *nl = 0;
}

static void copy(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src);
}

/* ---------- config/state files ---------- */

static void config_defaults(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.timeout_s = 10;
    cfg.bright_day = 200;
    cfg.bright_night = 40;
    cfg.night_start = 22;
    cfg.night_end = 7;
    cfg.show_call = cfg.show_missed = cfg.show_sms = 1;
    static const char *days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 7; ++i) copy(cfg.days[i], sizeof(cfg.days[i]), days[i]);
    for (int i = 0; i < 12; ++i) copy(cfg.months[i], sizeof(cfg.months[i]), months[i]);
    copy(cfg.lbl_incoming, sizeof(cfg.lbl_incoming), "Incoming call");
    copy(cfg.lbl_call, sizeof(cfg.lbl_call), "Call");
    copy(cfg.lbl_unknown, sizeof(cfg.lbl_unknown), "Unknown");
    copy(cfg.lbl_no_service, sizeof(cfg.lbl_no_service), "No service");
}

/* Calls fn(key, value) for each key=value line of a small text file. */
static bool parse_file(const char *path, void (*fn)(const char *, const char *)) {
    char buf[4096];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        fn(line, eq + 1);
    }
    return true;
}

static void split_list(const char *value, char *out, size_t item_size, int count) {
    int i = 0;
    const char *p = value;
    while (i < count && *p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len >= item_size) len = item_size - 1;
        memcpy(out + i * item_size, p, len);
        out[i * item_size + len] = 0;
        ++i;
        if (!comma) break;
        p = comma + 1;
    }
}

static void on_config(const char *k, const char *v) {
    int n = atoi(v);
    if (!strcmp(k, "enabled")) cfg.enabled = n;
    else if (!strcmp(k, "timeout_s")) cfg.timeout_s = n < 3 ? 3 : n > 120 ? 120 : n;
    else if (!strcmp(k, "bright_day")) cfg.bright_day = n < 1 ? 1 : n > 255 ? 255 : n;
    else if (!strcmp(k, "bright_night")) cfg.bright_night = n < 1 ? 1 : n > 255 ? 255 : n;
    else if (!strcmp(k, "night_start")) cfg.night_start = n % 24;
    else if (!strcmp(k, "night_end")) cfg.night_end = n % 24;
    else if (!strcmp(k, "show_call")) cfg.show_call = n;
    else if (!strcmp(k, "show_missed")) cfg.show_missed = n;
    else if (!strcmp(k, "show_sms")) cfg.show_sms = n;
    else if (!strcmp(k, "sms_text")) cfg.sms_text = n;
    else if (!strcmp(k, "days")) split_list(v, &cfg.days[0][0], sizeof(cfg.days[0]), 7);
    else if (!strcmp(k, "months")) split_list(v, &cfg.months[0][0], sizeof(cfg.months[0]), 12);
    else if (!strcmp(k, "lbl_incoming")) copy(cfg.lbl_incoming, sizeof(cfg.lbl_incoming), v);
    else if (!strcmp(k, "lbl_call")) copy(cfg.lbl_call, sizeof(cfg.lbl_call), v);
    else if (!strcmp(k, "lbl_unknown")) copy(cfg.lbl_unknown, sizeof(cfg.lbl_unknown), v);
    else if (!strcmp(k, "lbl_no_service")) copy(cfg.lbl_no_service, sizeof(cfg.lbl_no_service), v);
}

static void on_state(const char *k, const char *v) {
    if (!strcmp(k, "call")) st.call = !strcmp(v, "ringing") ? 1 : !strcmp(v, "active") ? 2 : 0;
    else if (!strcmp(k, "call_name")) copy(st.call_name, sizeof(st.call_name), v);
    else if (!strcmp(k, "call_number")) copy(st.call_number, sizeof(st.call_number), v);
    else if (!strcmp(k, "call_since")) st.call_since_ms = atoll(v);
    else if (!strcmp(k, "missed")) st.missed = atoi(v);
    else if (!strcmp(k, "missed_name")) copy(st.missed_name, sizeof(st.missed_name), v);
    else if (!strcmp(k, "sms")) st.sms = atoi(v);
    else if (!strcmp(k, "sms_name")) copy(st.sms_name, sizeof(st.sms_name), v);
    else if (!strcmp(k, "sms_body")) copy(st.sms_body, sizeof(st.sms_body), v);
    else if (!strcmp(k, "signal")) st.signal = atoi(v);
    else if (!strcmp(k, "wifi")) st.wifi = atoi(v);
    else if (!strcmp(k, "operator")) copy(st.operator_name, sizeof(st.operator_name), v);
    else if (!strcmp(k, "seq")) st.seq = atoll(v);
}

static void load_config(void) {
    char path[PATH_MAX];
    config_defaults();
    snprintf(path, sizeof(path), "%s/config.txt", app_dir);
    parse_file(path, on_config);
}

static void load_state(void) {
    char path[PATH_MAX];
    memset(&st, 0, sizeof(st));
    st.signal = st.wifi = -1;
    snprintf(path, sizeof(path), "%s/state.txt", app_dir);
    parse_file(path, on_state);
}

/* ---------- drawing ---------- */

static void clear(void) {
    for (int i = 0; i < W * H; ++i) canvas[i] = C_BG;
}

static void blend(int x, int y, uint32_t color, int alpha) {
    if (x < 0 || y < 0 || x >= W || y >= H || alpha <= 0) return;
    uint32_t *p = &canvas[y * W + x];
    if (alpha >= 255) { *p = color; return; }
    uint32_t d = *p;
    int r = (((color >> 16) & 255) * alpha + ((d >> 16) & 255) * (255 - alpha)) / 255;
    int g = (((color >> 8) & 255) * alpha + ((d >> 8) & 255) * (255 - alpha)) / 255;
    int b = ((color & 255) * alpha + (d & 255) * (255 - alpha)) / 255;
    *p = RGB(r, g, b);
}

static void fill(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) blend(i, j, color, 255);
}

/* Decodes one UTF-8 code point; advances *s. Invalid bytes become '?'. */
static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
    else { *s += 1; return '?'; }
    for (int i = 1; i <= extra; ++i) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return '?'; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

static const cover_glyph_t *glyph(const cover_font_t *f, uint32_t cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return cp == '?' ? NULL : glyph(f, '?');
}

static int text_width(const cover_font_t *f, const char *s) {
    int w = 0;
    while (*s) {
        const cover_glyph_t *g = glyph(f, utf8_next(&s));
        if (g) w += g->advance;
    }
    return w;
}

/* Draws s with its baseline at y; returns the x after the text. */
static int draw_text(const cover_font_t *f, int x, int y, const char *s, uint32_t color) {
    while (*s) {
        const cover_glyph_t *g = glyph(f, utf8_next(&s));
        if (!g) continue;
        const uint8_t *a = f->alpha + g->offset;
        for (int j = 0; j < g->h; ++j)
            for (int i = 0; i < g->w; ++i)
                blend(x + g->left + i, y - g->top + j, color, a[j * g->w + i]);
        x += g->advance;
    }
    return x;
}

/* Copies as much of s as fits in max_w pixels, adding an ellipsis if cut. */
static void fit(const cover_font_t *f, const char *s, int max_w, char *out, size_t size) {
    if (text_width(f, s) <= max_w) { copy(out, size, s); return; }
    int ell = text_width(f, "\xE2\x80\xA6");
    const char *p = s;
    int w = 0;
    while (*p) {
        const char *start = p;
        const cover_glyph_t *g = glyph(f, utf8_next(&p));
        int adv = g ? g->advance : 0;
        if (w + adv + ell > max_w) { p = start; break; }
        w += adv;
    }
    size_t len = (size_t)(p - s);
    if (len > size - 4) len = size - 4;
    memcpy(out, s, len);
    memcpy(out + len, "\xE2\x80\xA6", 4);
}

static void draw_centered(const cover_font_t *f, int y, const char *s, uint32_t color) {
    char buf[200];
    fit(f, s, W - 6, buf, sizeof(buf));
    draw_text(f, (W - text_width(f, buf)) / 2, y, buf, color);
}

/* Splits a name over up to two centered lines at the last space that fits. */
static void draw_name(const cover_font_t *f, int y, const char *s, uint32_t color) {
    if (text_width(f, s) <= W - 6) { draw_centered(f, y, s, color); return; }
    char first[200];
    copy(first, sizeof(first), s);
    char *cut = NULL;
    for (char *p = strchr(first, ' '); p; p = strchr(p + 1, ' ')) {
        *p = 0;
        if (text_width(f, first) <= W - 6) cut = p;
        *p = ' ';
    }
    if (!cut) { draw_centered(f, y, s, color); return; }
    *cut = 0;
    draw_centered(f, y, first, color);
    draw_centered(f, y + f->height, cut + 1, color);
}

/* Tiny monochrome icons, '#' = pixel. */
static void draw_icon(int x, int y, const char *const *rows, int n, uint32_t color) {
    for (int j = 0; j < n; ++j)
        for (int i = 0; rows[j][i]; ++i)
            if (rows[j][i] == '#') blend(x + i, y + j, color, 255);
}

static const char *const ICON_PHONE[] = {
    "##.......", "###......", "###......", ".##......", ".##......",
    "..##.....", "...###.##", "....#####", ".....####"};
static const char *const ICON_SMS[] = {
    "#########", "##.....##", "#.#...#.#", "#..#.#..#", "#...#...#",
    "#.......#", "#########"};

static void draw_signal(int x, int y, int level) {
    for (int i = 0; i < 4; ++i) {
        int h = 3 + i * 2;
        fill(x + i * 3, y + 9 - h, 2, h, level > i ? C_TEXT : RGB(70, 70, 70));
    }
}

static void draw_wifi(int x, int y, int level) {
    /* three arcs approximated with steps, plus a dot */
    uint32_t on = C_TEXT, off = RGB(70, 70, 70);
    fill(x + 4, y + 8, 2, 2, level >= 1 ? on : off);
    fill(x + 2, y + 5, 6, 1, level >= 2 ? on : off);
    fill(x + 1, y + 6, 1, 1, level >= 2 ? on : off);
    fill(x + 8, y + 6, 1, 1, level >= 2 ? on : off);
    fill(x + 1, y + 2, 8, 1, level >= 3 ? on : off);
    fill(x, y + 3, 1, 1, level >= 3 ? on : off);
    fill(x + 9, y + 3, 1, 1, level >= 3 ? on : off);
}

static void draw_battery(int right, int y, int pct, bool charging) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    int tw = text_width(&font_small, buf);
    int bx = right - 14;
    fill(bx, y + 1, 12, 1, C_TEXT); fill(bx, y + 8, 12, 1, C_TEXT);
    fill(bx, y + 1, 1, 8, C_TEXT); fill(bx + 11, y + 1, 1, 8, C_TEXT);
    fill(bx + 12, y + 3, 2, 4, C_TEXT);
    int level = pct * 9 / 100;
    fill(bx + 1, y + 2, level < 1 ? 1 : level, 6,
         charging ? C_GOOD : pct <= 15 ? C_ALERT : C_TEXT);
    draw_text(&font_small, bx - tw - 2, y + 10, buf, C_TEXT);
}

static void draw_status_row(void) {
    int x = 3;
    if (st.signal >= 0) { draw_signal(x, 2, st.signal); x += 14; }
    if (st.wifi >= 0) { draw_wifi(x, 2, st.wifi); x += 13; }
    char status[16];
    read_text(BATTERY "status", status, sizeof(status));
    draw_battery(W - 2, 2, read_int(BATTERY "capacity", 0),
                 !strcmp(status, "Charging") || !strcmp(status, "Full"));
}

static void draw_counter_row(int y, const char *const *icon, int icon_rows, int count,
                             const char *who, uint32_t color) {
    draw_icon(4, y - 9, icon, icon_rows, color);
    char buf[160], line[200];
    if (who[0]) snprintf(buf, sizeof(buf), "%d \xC2\xB7 %s", count, who);
    else snprintf(buf, sizeof(buf), "%d", count);
    fit(&font_small, buf, W - 20, line, sizeof(line));
    draw_text(&font_small, 17, y, line, C_TEXT);
}

static void render_idle(const struct tm *tm) {
    draw_status_row();
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", tm->tm_hour, tm->tm_min);
    draw_text(&font_clock, (W - text_width(&font_clock, clock)) / 2, 60, clock, C_TEXT);
    char date[64];
    snprintf(date, sizeof(date), "%s, %d %s", cfg.days[tm->tm_wday], tm->tm_mday,
             cfg.months[tm->tm_mon]);
    draw_centered(&font_small, 78, date, C_DIM);
    int y = 100;
    bool any = false;
    if (cfg.show_missed && st.missed > 0) {
        draw_counter_row(y, ICON_PHONE, 9, st.missed, st.missed_name, C_ALERT);
        y += 17; any = true;
    }
    if (cfg.show_sms && st.sms > 0) {
        const char *who = cfg.sms_text && st.sms_body[0] ? st.sms_body : st.sms_name;
        draw_counter_row(y, ICON_SMS, 7, st.sms, who, C_ACCENT);
        any = true;
    }
    if (!any) {
        const char *op = st.operator_name[0] ? st.operator_name
                         : st.signal < 0 ? cfg.lbl_no_service : "";
        draw_centered(&font_small, 108, op, C_DIM);
    }
}

static void render_call(const struct tm *tm) {
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", tm->tm_hour, tm->tm_min);
    draw_text(&font_small, 4, 13, clock, C_DIM);
    char status[16];
    read_text(BATTERY "status", status, sizeof(status));
    draw_battery(W - 2, 2, read_int(BATTERY "capacity", 0),
                 !strcmp(status, "Charging") || !strcmp(status, "Full"));
    bool ringing = st.call == 1;
    uint32_t accent = ringing ? C_ACCENT : C_GOOD;
    fill(0, 19, W, 18, accent);
    draw_centered(&font_small, 32, ringing ? cfg.lbl_incoming : cfg.lbl_call, C_BG);
    const char *name = st.call_name[0] ? st.call_name
                       : st.call_number[0] ? st.call_number : cfg.lbl_unknown;
    draw_name(&font_medium, 62, name, C_TEXT);
    if (st.call_name[0] && st.call_number[0])
        draw_centered(&font_small, 100, st.call_number, C_DIM);
    if (!ringing && st.call_since_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long long secs = ((long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 - st.call_since_ms) / 1000;
        if (secs < 0) secs = 0;
        char timer[16];
        snprintf(timer, sizeof(timer), "%lld:%02lld", secs / 60, secs % 60);
        draw_centered(&font_medium, 122, timer, C_GOOD);
    } else if (ringing) {
        draw_icon((W - 9) / 2, 108, ICON_PHONE, 9, (time(NULL) & 1) ? C_ACCENT : C_DIM);
    }
}

/* ---------- panel ---------- */

static bool fb_open(void) {
    if (fb_fd >= 0) return true;
    fb_fd = open(FB_DEV, O_RDWR | O_CLOEXEC);
    if (fb_fd < 0) { note("fb_open_failed", errno); return false; }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        note("fb_vinfo_failed", errno);
        close(fb_fd); fb_fd = -1;
        return false;
    }
    fb_map = mmap(NULL, W * H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_map == MAP_FAILED) {
        note("fb_mmap_failed", errno);
        fb_map = NULL; close(fb_fd); fb_fd = -1;
        return false;
    }
    return true;
}

static void flush(void) {
    if (!fb_open()) return;
    memcpy(fb_map, canvas, sizeof(canvas));
    /* The SPI panel is refreshed by these ioctls (software path). */
    vinfo.yoffset = 0;
    ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
}

static bool is_night(const struct tm *tm) {
    int h = tm->tm_hour;
    if (cfg.night_start == cfg.night_end) return false;
    return cfg.night_start < cfg.night_end ? (h >= cfg.night_start && h < cfg.night_end)
                                           : (h >= cfg.night_start || h < cfg.night_end);
}

static void compose(void) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    clear();
    if (cfg.show_call && st.call) render_call(&tm); else render_idle(&tm);
}

static void render(void) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    compose();
    /* Self-expiring lock: a crashed daemon cannot keep the phone awake. */
    write_node("/sys/power/wake_lock", WAKELOCK " 3000000000\n");
    flush();
    char level[8];
    snprintf(level, sizeof(level), "%d\n", is_night(&tm) ? cfg.bright_night : cfg.bright_day);
    write_node(BACKLIGHT, level);
}

static void screen_on(void) {
    write_node("/sys/power/wake_lock", WAKELOCK " 3000000000\n");
    if (fb_open()) ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    lit = true;
    note("on", 0);
}

static void screen_off(void) {
    if (!lit) return;
    clear();
    flush();
    /* The mdss driver ignores brightness 0 while the panel is unblanked; only
     * powering the panel down turns the backlight off. */
    if (fb_fd >= 0) ioctl(fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
    write_node(BACKLIGHT, "0\n");
    write_node("/sys/power/wake_unlock", WAKELOCK "\n");
    lit = false;
    note("off", 0);
}

/* ---------- events ---------- */

static int open_uevent(void) {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) return -1;
    struct sockaddr_nl addr = {.nl_family = AF_NETLINK, .nl_groups = 1};
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

static bool drain_uevents(int fd) {
    bool hall = false;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = 0;
        for (ssize_t i = 0; i < n; i += strlen(buf + i) + 1)
            if (strstr(buf + i, "hall")) hall = true;
    }
    return hall;
}

static bool drain_inotify(int fd) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool changed = false;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *e = (struct inotify_event *)p;
            if (e->len && (!strcmp(e->name, "config.txt") || !strcmp(e->name, "state.txt")))
                changed = true;
            if (e->mask & (IN_DELETE_SELF | IN_IGNORED)) changed = true;
            p += sizeof(*e) + e->len;
        }
    }
    return changed;
}

static void interrupted(int sig) { (void)sig; stopping = 1; }

int main(int argc, char **argv) {
    const char *preview = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) app_dir = argv[++i];
        else if (!strcmp(argv[i], "--preview") && i + 1 < argc) preview = argv[++i];
        else { fprintf(stderr, "usage: %s [--dir DIR] [--preview OUT.raw]\n", argv[0]); return 2; }
    }
    if (preview) {
        /* Development aid: draw the current screen into a raw 128x128 ARGB file
         * without touching the panel, backlight or wakelocks. */
        load_config();
        load_state();
        compose();
        FILE *out = fopen(preview, "wb");
        if (!out) { perror(preview); return 1; }
        fwrite(canvas, sizeof(canvas), 1, out);
        fclose(out);
        return 0;
    }

    struct sigaction action = {0};
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    int uevent = open_uevent();
    int inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int watch = -1;
    load_config();
    load_state();
    int lid = read_int(LID_STATE, 1);
    int64_t wake_until = lid == 0 ? now_ms() + cfg.timeout_s * 1000LL : 0;
    int prev_call = st.call;
    long long prev_seq = st.seq;
    note("start_lid", lid);

    while (!stopping) {
        if (watch < 0 && inotify >= 0) {
            watch = inotify_add_watch(inotify, app_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE_SELF);
            if (watch >= 0) { load_config(); load_state(); }
        }
        int64_t now = now_ms();
        bool calling = cfg.show_call && st.call != 0;
        bool want = cfg.enabled && lid == 0 && (calling || now < wake_until);
        if (want && !lit) screen_on();
        if (!want && lit) screen_off();

        int timeout = -1;
        if (lit) {
            render();
            timeout = 1000 - (int)(now % 1000);
            if (!calling && wake_until - now < timeout) timeout = (int)(wake_until - now);
            if (timeout < 1) timeout = 1;
        } else if (watch < 0) {
            timeout = 30000;  /* app not run yet (first boot): retry the watch */
        }

        struct pollfd fds[2] = {{.fd = uevent, .events = POLLIN}, {.fd = inotify, .events = POLLIN}};
        int ready = poll(fds, 2, timeout);
        if (ready < 0 && errno != EINTR) { note("poll_failed", errno); break; }
        if (ready <= 0) continue;

        if ((fds[0].revents & POLLIN) && drain_uevents(uevent)) {
            int value = read_int(LID_STATE, lid);
            if (value != lid) {
                lid = value;
                note("lid", lid);
                wake_until = lid == 0 ? now_ms() + cfg.timeout_s * 1000LL : 0;
            }
        }
        if ((fds[1].revents & POLLIN) && drain_inotify(inotify)) {
            if (access(app_dir, F_OK) != 0 && watch >= 0) {
                inotify_rm_watch(inotify, watch);
                watch = -1;
            }
            load_config();
            load_state();
            int64_t t = now_ms();
            if (st.seq > prev_seq && (cfg.show_missed || cfg.show_sms)) {
                note("new_event", st.seq);
                wake_until = t + cfg.timeout_s * 1000LL;
            }
            if (prev_call != 0 && st.call == 0) wake_until = t + cfg.timeout_s * 1000LL;
            if (st.call != prev_call) note("call", st.call);
            prev_seq = st.seq;
            prev_call = st.call;
        }
    }
    screen_off();
    note("exit", 0);
    return 0;
}
