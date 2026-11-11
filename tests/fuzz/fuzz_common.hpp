// SPDX-License-Identifier: GPL-3.0-only
// Shared by the libFuzzer targets in this folder (scripts/fuzz.sh builds and runs them).
#pragma once
#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

// A property that must hold for every input. Unlike assert() it never depends on NDEBUG, and it says which one broke.
#define FUZZ_CHECK(cond)                                                                                 \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            std::fprintf(stderr, "FUZZ_CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            std::abort();                                                                                \
        }                                                                                                \
    } while (0)

namespace fuzz {

// A scratch folder of this process, for the targets that read and write files: /dev/shm when there is one, so that
// millions of runs don't wear a disk. Removed at exit.
inline const std::filesystem::path &scratch() {
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        std::filesystem::path base = std::filesystem::is_directory("/dev/shm", ec) ? "/dev/shm" : std::filesystem::temp_directory_path();
        auto path = base / ("prospero_fuzz." + std::to_string(::getpid()));
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        std::atexit([] {
            std::error_code gone;
            std::filesystem::remove_all(scratch(), gone);
        });
        return path;
    }();
    return dir;
}

inline std::string lower(std::string text) {
    for (char &c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

}  // namespace fuzz
