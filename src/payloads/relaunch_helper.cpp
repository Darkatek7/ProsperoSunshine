// SPDX-License-Identifier: GPL-3.0-only
// "Restart host" on the web page. The host can't send its own ELF to the loader: the loader would start the new host while
// the old process is still going away, and once on 10.20 the console hung right then (PR 27). So the host sends this
// payload (embedded in it), writes its process id to <appdata>/relaunch.pid and shuts down. This waits until that process
// is gone, gives the system two more seconds, as the app does after QUIT, and then sends the installed host to the loader.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

namespace {
constexpr const char *data_dir = "/data/prosperosunshine";
constexpr int loader_port = 9021;

// One line in host_diag.log, which the app's and the web page's reports read, and in the kernel log.
void note(const char *text) {
    klog_printf("ProsperoSunshine: relaunch %s\n", text);
    char path[128];
    std::snprintf(path, sizeof path, "%s/host_diag.log", data_dir);
    if (FILE *file = std::fopen(path, "a")) {
        std::fprintf(file, "ProsperoSunshine: relaunch %s\n", text);
        std::fclose(file);
    }
}

bool alive(pid_t pid) { return kill(pid, 0) == 0 || errno == EPERM; }

const char *send_host() {
    char path[128];
    std::snprintf(path, sizeof path, "%s/prospero_sunshine.elf", data_dir);
    const int file = open(path, O_RDONLY);
    if (file < 0) return "no host ELF in the data folder";
    struct stat info {};
    if (fstat(file, &info) != 0 || info.st_size <= 0) {
        close(file);
        return "the host ELF is empty";
    }
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        close(file);
        return "no socket";
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    timeval timeout{10, 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(loader_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const char *problem = nullptr;
    if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) problem = "the ELF loader did not answer";
    char buffer[64 * 1024];
    for (off_t left = info.st_size; !problem && left > 0;) {
        const ssize_t got = read(file, buffer, sizeof buffer);
        if (got <= 0) {
            problem = "reading the host ELF failed";
            break;
        }
        for (ssize_t sent = 0; !problem && sent < got;) {
            const ssize_t n = send(sock, buffer + sent, static_cast<size_t>(got - sent), 0);
            if (n <= 0) problem = "sending to the ELF loader failed";
            else sent += n;
        }
        left -= got;
    }
    close(sock);
    close(file);
    return problem;
}
}  // namespace

int main() {
    char path[128];
    std::snprintf(path, sizeof path, "%s/relaunch.pid", data_dir);
    pid_t pid = 0;
    if (FILE *file = std::fopen(path, "r")) {
        long value = 0;
        if (std::fscanf(file, "%ld", &value) == 1) pid = static_cast<pid_t>(value);
        std::fclose(file);
    }
    unlink(path);
    if (pid <= 0) {
        note("found no host to wait for; not starting one");
        return 1;
    }
    // The host stops within a few seconds; after a minute something is wrong, and starting a second host would be worse.
    int waited = 0;
    for (; alive(pid) && waited < 60000; waited += 250) usleep(250 * 1000);
    if (alive(pid)) {
        note("the old host is still running after 60 s; not starting another");
        return 1;
    }
    sleep(2);
    const char *problem = send_host();
    char line[160];
    std::snprintf(line, sizeof line, "after the old host was gone (%d ms): %s", waited,
                  problem ? problem : "sent the host to the ELF loader");
    note(line);
    return problem ? 1 : 0;
}
