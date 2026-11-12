/* Keypad backlight controller: lit only while the lid is open AND the main
 * panel is powered.
 *
 * The lights HAL (lights-service.qti) exposes no BUTTONS/KEYBOARD light, so
 * the framework cannot drive keyboard_light itself. Both inputs are
 * event-driven, there is no periodic polling:
 *   - lid: /sys/devices/virtual/switch_hall/hall_switch/state (1 open, 0
 *     closed), re-read on kernel uevents. The hall_sensor input device never
 *     reports SW_LID on this kernel.
 *   - panel: mdss show_blank_event ("panel_power_on = N"), which the driver
 *     sysfs_notify()s on every blank/unblank.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static const char *lid_path = "/sys/devices/virtual/switch_hall/hall_switch/state";
static const char *panel_path = "/sys/class/graphics/fb0/show_blank_event";
static const char *light_path = "/sys/class/leds/keyboard_light/brightness";
static volatile sig_atomic_t stopping;

static void note(const char *event, int value) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    fprintf(stderr, "keylight t=%lld event=%s value=%d\n",
            (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000, event, value);
    fflush(stderr);
}

static int read_number(int fd, const char *prefix) {
    char buf[64] = {0};
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    const char *p = buf;
    if (prefix) {
        p = strstr(buf, prefix);
        if (!p) return -1;
        p += strlen(prefix);
    }
    while (*p == ' ' || *p == '=') ++p;
    if (*p < '0' || *p > '9') return -1;
    return atoi(p);
}

static bool write_light(int value) {
    int fd = open(light_path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", value);
    bool ok = write(fd, buf, len) == len;
    close(fd);
    return ok;
}

static int open_uevent(void) {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) return -1;
    struct sockaddr_nl addr = {.nl_family = AF_NETLINK, .nl_groups = 1};
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool drain_uevents(int fd) {
    bool hall = false;
    char buf[4096];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        for (ssize_t i = 0; i < n; i += strlen(buf + i) + 1)
            if (strstr(buf + i, "hall")) hall = true;
    }
    return hall;
}

static void interrupted(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

int main(int argc, char **argv) {
    int brightness = 255;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--brightness") && i + 1 < argc) brightness = atoi(argv[++i]);
        else return 2;
    }
    if (brightness < 1 || brightness > 255) return 2;

    int lid_fd = open(lid_path, O_RDONLY | O_CLOEXEC);
    int panel_fd = open(panel_path, O_RDONLY | O_CLOEXEC);
    if (lid_fd < 0 || panel_fd < 0) {
        perror(lid_fd < 0 ? lid_path : panel_path);
        return 3;
    }
    int uevent = open_uevent();
    if (uevent < 0) {
        perror("uevent");
        return 3;
    }
    struct sigaction action = {0};
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    int lid = read_number(lid_fd, NULL);
    int panel = read_number(panel_fd, "panel_power_on");
    int lit = -1;
    note("start_lid", lid);
    note("start_panel", panel);
    while (!stopping) {
        int want = (lid == 1 && panel == 1) ? brightness : 0;
        if (want != lit) {
            if (write_light(want)) {
                lit = want;
                note("light", want);
            } else {
                note("write_failed", errno);
            }
        }
        struct pollfd fds[2] = {
            {.fd = uevent, .events = POLLIN},
            /* sysfs_notify() wakes POLLPRI|POLLERR; the value must be re-read. */
            {.fd = panel_fd, .events = POLLPRI | POLLERR},
        };
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            note("poll_failed", errno);
            break;
        }
        if ((fds[0].revents & POLLIN) && drain_uevents(uevent)) {
            int value = read_number(lid_fd, NULL);
            if (value >= 0 && value != lid) note("lid", lid = value);
        }
        if (fds[1].revents & (POLLPRI | POLLERR)) {
            int value = read_number(panel_fd, "panel_power_on");
            if (value >= 0 && value != panel) note("panel", panel = value);
        }
    }
    write_light(0);
    note("exit", 0);
    return 0;
}
