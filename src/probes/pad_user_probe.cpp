// SPDX-License-Identifier: GPL-3.0-only
// Second player: lists the signed-in users, then adds one virtual pad for the user that
// /data/prosperosunshine/pad_user_probe.user names, holds it neutral for a while and removes it again.
//   pad_user_probe.user  a user ID in hex (one from the list), "none" for 0xFFFFFFFF, "zero" for 0, or "list" to only
//                        list the users; default "list".
//   pad_user_probe.hold  seconds to keep the pad, default 30.
// The question is whether the system takes a pad for another user than the first one (a guest, say), or asks "Who's using
// this controller?" for a pad without one. It presses no buttons. Writes /data/prosperosunshine/pad_user_probe.log.
#include "platform/ps5/pad.h"
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
extern "C" {
#include <ps5/klog.h>
int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);
int sceUserServiceGetForegroundUser(uint32_t *);
int sceUserServiceGetInitialUser(uint32_t *);
int sceUserServiceGetLoginUserIdList(int32_t *);
}

namespace {

FILE *log_file = nullptr;

void say(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    klog_printf("ProsperoSunshine: pad_user %s\n", line);
    if (log_file) {
        fprintf(log_file, "%s\n", line);
        fflush(log_file);
    }
}

std::string read_word(const char *path, const char *fallback) {
    std::ifstream in(path);
    std::string word;
    in >> word;
    return word.empty() ? fallback : word;
}

}  // namespace

int main() {
    using namespace std::chrono;
    log_file = fopen("/data/prosperosunshine/pad_user_probe.log", "w");
    const std::string which = read_word("/data/prosperosunshine/pad_user_probe.user", "list");
    const int hold = std::atoi(read_word("/data/prosperosunshine/pad_user_probe.hold", "30").c_str());

    const int init = sceUserServiceInitialize(nullptr);
    uint32_t foreground = 0, initial = 0;
    const int fg_rc = sceUserServiceGetForegroundUser(&foreground);
    const int in_rc = sceUserServiceGetInitialUser(&initial);
    say("init=%#x foreground=%#x (rc %#x) initial=%#x (rc %#x) home_first=%#x", init, foreground, fg_rc, initial, in_rc,
        ps_pad_default_user());
    // The list holds four IDs on the PS4; room for more in case the PS5 writes more. -1 is an empty slot.
    int32_t users[16];
    for (auto &user : users) user = -1;
    const int list_rc = sceUserServiceGetLoginUserIdList(users);
    say("login list rc=%#x: %#x %#x %#x %#x", list_rc, users[0], users[1], users[2], users[3]);

    int result = 0;
    if (which != "list") {
        const uint32_t user = which == "none" ? PS_PAD_NO_USER
                              : which == "zero" ? 0u
                                                : static_cast<uint32_t>(std::strtoul(which.c_str(), nullptr, 16));
        ps_pad *pad = nullptr;
        const int open_rc = ps_pad_open(user, &pad);
        say("open user=%#x rc=%d", user, open_rc);
        if (open_rc) {
            result = 1;
        } else {
            int sends = 0, errors = 0;
            const auto end = steady_clock::now() + seconds(hold);
            while (steady_clock::now() < end) {
                errors += ps_pad_neutral(pad) != 0;
                ++sends;
                std::this_thread::sleep_for(16ms);
            }
            const int close_rc = ps_pad_close(pad);
            say("held %d s: sends=%d errors=%d close_rc=%d", hold, sends, errors, close_rc);
            result = errors || close_rc;
        }
        for (auto &u : users) u = -1;
        say("login list after rc=%#x: %#x %#x %#x %#x", sceUserServiceGetLoginUserIdList(users), users[0], users[1],
            users[2], users[3]);
    }
    if (init == 0) sceUserServiceTerminate();
    say("done");
    if (log_file) fclose(log_file);
    return result;
}
