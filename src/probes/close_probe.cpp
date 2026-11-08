// SPDX-License-Identifier: GPL-3.0-only
// Closes the title in front through the system's own close call, as for "Terminate App" in Moonlight, and checks what
// follows: how long the title takes to leave the front and its /mnt/sandbox folder, and whether it starts again afterwards
// (ShadowMountPlus releases a title after it ends; drakmor/ShadowMountPlus#195). Writes /data/prosperosunshine/close_probe.log.
//   close_probe.title  title to close, default PPSA03527; anything else in front is left alone.
//   close_probe.method ss = sceSystemServiceKillApp(app, -1, 0, 0) (default), lnc = sceLncUtilKillApp(app),
//                      reason = sceLncUtilKillAppWithReason(app, 0).
//   close_probe.delay  seconds to wait after the sandbox is gone before starting the title again, default 0; -1 = don't;
//                      auto = as ShadowMountPlus decides the exit: until no /mnt/sandbox/<title>_* entry is left, even an
//                      empty one, then 1.5 s more (its grace is 1 s), at most 11 s after the close returned (its linger is
//                      10 s from the exit).
// On 10.20 with ShadowMountPlus 1.7beta11, a start right after the close came while it was still releasing the title: the
// game ran, but ShadowMountPlus never bound it ("runtime preparation timed out").
// Never closes system apps (NPXS) or the settings app. Unsaved progress in the closed title is lost.
#include "platform/ps5/diag.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <thread>
extern "C" {
#include <ps5/kernel.h>
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t, char *);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceLncUtilKillApp(uint32_t app_id);
int sceLncUtilKillAppWithReason(uint32_t app_id, int reason);
int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);
int sceUserServiceGetForegroundUser(uint32_t *);
struct launch_context {
    uint32_t size, user_id, options;
    uint64_t crash_report;
    uint32_t flags;
};
int sceSystemServiceLaunchApp(const char *, char **, launch_context *);
}

using namespace std::chrono;

namespace {

std::string read_word(const char *path, const char *fallback) {
    std::ifstream in(path);
    std::string word;
    in >> word;
    return word.empty() ? fallback : word;
}

std::string front() {
    char title[16] = {};
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    if (app <= 0 || sceLncUtilGetAppTitleId(static_cast<uint32_t>(app), title) != 0) return "none";
    return title;
}

// A running title has a non-empty /mnt/sandbox/<title>_NNN. After ShadowMountPlus missed an exit, an empty <title>_000 stayed
// and the next start got _001, so empty folders don't count.
bool sandbox(const std::string &title) {
    DIR *root = opendir("/mnt/sandbox");
    if (!root) return false;
    bool found = false;
    while (const dirent *entry = readdir(root)) {
        const std::string name = entry->d_name;
        if (found || name.size() != title.size() + 4 || name.compare(0, title.size() + 1, title + "_") != 0) continue;
        if (DIR *dir = opendir(("/mnt/sandbox/" + name).c_str())) {
            while (const dirent *inner = readdir(dir))
                if (std::strcmp(inner->d_name, ".") != 0 && std::strcmp(inner->d_name, "..") != 0) found = true;
            closedir(dir);
        }
    }
    closedir(root);
    return found;
}

int launch(const std::string &title) {
    const int already = static_cast<int>(0x80960003);
    const int init = sceUserServiceInitialize(nullptr);
    if (init && init != already) return init;
    launch_context context{};
    context.size = sizeof context;
    int rc = sceUserServiceGetForegroundUser(&context.user_id);
    char *args[] = {nullptr};
    if (rc == 0) rc = sceSystemServiceLaunchApp(title.c_str(), args, &context);
    if (init == 0) sceUserServiceTerminate();
    return rc;
}

// What ShadowMountPlus checks before it finalizes an exit: any /mnt/sandbox/<title>_* entry, empty or not.
bool any_entry(const std::string &title) {
    DIR *root = opendir("/mnt/sandbox");
    if (!root) return false;
    bool found = false;
    while (const dirent *entry = readdir(root))
        if (std::strncmp(entry->d_name, title.c_str(), title.size()) == 0 && entry->d_name[title.size()] == '_') found = true;
    closedir(root);
    return found;
}

}  // namespace

int main() {
    const std::string target = read_word("/data/prosperosunshine/close_probe.title", "PPSA03527");
    const std::string method = read_word("/data/prosperosunshine/close_probe.method", "ss");
    const std::string delay_word = read_word("/data/prosperosunshine/close_probe.delay", "0");
    const bool automatic = delay_word == "auto";
    const int delay = automatic ? 0 : std::stoi(delay_word);
    char header[128];
    snprintf(header, sizeof header, "ProsperoSunshine: close_probe firmware=%#x title=%s method=%s delay=%s",
             kernel_get_fw_version(), target.c_str(), method.c_str(), delay_word.c_str());
    ps_diag_open("/data/prosperosunshine/close_probe.log", header);

    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    const std::string in_front = front();
    ps_diag_log("ProsperoSunshine: close_probe front=%s app_id=%#x sandbox=%d\n", in_front.c_str(), static_cast<unsigned>(app),
                sandbox(target));
    if (in_front != target || target.rfind("NPXS", 0) == 0 || target == "PPSA47989" || app <= 0) {
        ps_diag_log("ProsperoSunshine: close_probe refused: %s is not the title in front\n", target.c_str());
        return 1;
    }

    const auto start = steady_clock::now();
    const auto ms = [&] { return static_cast<long>(duration_cast<milliseconds>(steady_clock::now() - start).count()); };
    int rc = 0;
    if (method == "lnc") rc = sceLncUtilKillApp(static_cast<uint32_t>(app));
    else if (method == "reason") rc = sceLncUtilKillAppWithReason(static_cast<uint32_t>(app), 0);
    else rc = sceSystemServiceKillApp(app, -1, 0, 0);
    const long call_ms = ms();
    ps_diag_log("ProsperoSunshine: close_probe call rc=%#x call_ms=%ld\n", static_cast<unsigned>(rc), call_ms);
    if (rc < 0) return 1;

    std::string last_front = target;
    bool last_sandbox = true;
    long front_gone = -1, sandbox_gone = -1;
    while (ms() < 15000 && (front_gone < 0 || sandbox_gone < 0)) {
        std::this_thread::sleep_for(100ms);
        const std::string now_front = front();
        const bool now_sandbox = sandbox(target);
        if (now_front != last_front || now_sandbox != last_sandbox)
            ps_diag_log("ProsperoSunshine: close_probe t_ms=%ld front=%s sandbox=%d\n", ms(), now_front.c_str(), now_sandbox);
        if (front_gone < 0 && now_front != target) front_gone = ms();
        if (sandbox_gone < 0 && !now_sandbox) sandbox_gone = ms();
        last_front = now_front;
        last_sandbox = now_sandbox;
    }
    ps_diag_log("ProsperoSunshine: close_probe %s front_gone_ms=%ld sandbox_gone_ms=%ld\n",
                front_gone >= 0 && sandbox_gone >= 0 ? "closed" : "timeout", front_gone, sandbox_gone);
    if (sandbox_gone < 0 || delay < 0) return sandbox_gone < 0;
    if (automatic) {
        long entries_gone = -1;
        while (ms() < call_ms + 11000) {
            if (entries_gone < 0 && !any_entry(target)) {
                entries_gone = ms();
                ps_diag_log("ProsperoSunshine: close_probe t_ms=%ld no %s_* entry left\n", entries_gone, target.c_str());
            }
            if (entries_gone >= 0 && ms() >= entries_gone + 1500) break;
            std::this_thread::sleep_for(100ms);
        }
        ps_diag_log("ProsperoSunshine: close_probe waited until t_ms=%ld (%s)\n", ms(),
                    entries_gone >= 0 ? "entries gone + 1.5 s" : "cap of 11 s");
    } else {
        std::this_thread::sleep_for(seconds(delay));
    }

    // Starts it again: ShadowMountPlus must have released it (0x80940033 while it hasn't, retried as the host does).
    const auto relaunch = steady_clock::now();
    for (int attempt = 1;; attempt++) {
        rc = launch(target);
        ps_diag_log("ProsperoSunshine: close_probe launch try=%d rc=%#x t_ms=%ld\n", attempt, static_cast<unsigned>(rc),
                    static_cast<long>(duration_cast<milliseconds>(steady_clock::now() - relaunch).count()));
        if (rc != static_cast<int>(0x80940033) || steady_clock::now() - relaunch > 5s) break;
        std::this_thread::sleep_for(500ms);
    }
    for (int i = 0; i < 100 && front() != target; i++) std::this_thread::sleep_for(100ms);
    ps_diag_log("ProsperoSunshine: close_probe done front=%s\n", front().c_str());
    return 0;
}
