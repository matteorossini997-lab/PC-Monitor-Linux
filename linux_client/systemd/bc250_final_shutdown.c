/* Final systemd-shutdown hook: runtime services cannot authorize an ATX cut.
 * Compile on the target Linux machine. No Python, network or daemon is required
 * at this late stage. Reboot/kexec MUST NOT release the ATX latch.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int notify(const char *device)
{
    int fd = open(device, O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat info;
    struct termios settings;
    if (fstat(fd, &info) || !S_ISCHR(info.st_mode) || tcgetattr(fd, &settings)) {
        close(fd);
        return -1;
    }
    cfmakeraw(&settings);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~(HUPCL | CRTSCTS);
    cfsetispeed(&settings, B115200);
    cfsetospeed(&settings, B115200);
    if (tcsetattr(fd, TCSANOW, &settings)) { close(fd); return -1; }
    int lines = TIOCM_DTR | TIOCM_RTS;
    (void)ioctl(fd, TIOCMBIC, &lines);
    static const char event[] = "PWR_OS:OFF_CONFIRMED\n";
    for (int attempt = 0; attempt < 3; ++attempt) {
        struct pollfd writable = { .fd = fd, .events = POLLOUT };
        if (poll(&writable, 1, 250) <= 0 ||
            write(fd, event, sizeof(event) - 1) != (ssize_t)(sizeof(event) - 1)) {
            close(fd);
            return -1;
        }
        struct timespec delay = { .tv_nsec = 100000000 };
        nanosleep(&delay, NULL);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "poweroff") != 0 && strcmp(argv[1], "halt") != 0))
        return 0;
#ifdef BC250_HOOK_TEST
    /* Test builds may ONLY receive a pseudo-terminal from the test suite. */
    if (argc != 3 || strncmp(argv[2], "/dev/pts/", 9) != 0) return 1;
    return notify(argv[2]) == 0 ? 0 : 1;
#else
    char device[512];
    int config = open("/etc/bc250-power-device", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    if (config < 0) return 0; /* Missing notification must not block OS shutdown. */
    if (fstat(config, &info) || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 022) || info.st_size <= 0 || info.st_size >= (off_t)sizeof(device)) {
        close(config);
        return 0;
    }
    ssize_t n = read(config, device, sizeof(device) - 1);
    close(config);
    if (n <= 0) return 0;
    device[n] = '\0';
    device[strcspn(device, "\r\n")] = '\0';
    if (strncmp(device, "/dev/", 5) != 0 || notify(device) != 0)
        fputs("BC250: final poweroff notification failed; ATX must remain on\n", stderr);
    return 0;
#endif
}
