// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_login.hpp"
#include "web/web_auth.hpp"
#include "src/platform/common.h"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace prospero::web_login {

namespace {
// PBKDF2-SHA256; about a tenth of a second on the console, once per login.
constexpr unsigned iterations = 100000;

std::mutex mutex;
std::atomic<unsigned> changes{0};
bool loaded = false;
std::optional<web::Login> current;

std::filesystem::path file() { return platf::appdata() / "web_login"; }

std::string hex(const unsigned char *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

std::string random_hex(size_t bytes) {
    unsigned char buffer[64];
    if (bytes > sizeof buffer || RAND_bytes(buffer, static_cast<int>(bytes)) != 1) return {};
    return hex(buffer, bytes);
}

std::string pbkdf2(std::string_view password, std::string_view salt, unsigned rounds) {
    unsigned char out[32];
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                          static_cast<int>(rounds), EVP_sha256(), sizeof out, out) != 1)
        return {};
    return hex(out, sizeof out);
}

// Under the mutex.
const std::optional<web::Login> &login() {
    if (!loaded) {
        std::ostringstream text;
        text << std::ifstream(file(), std::ios::binary).rdbuf();
        current = web::parse_login(text.str());
        loaded = true;
    }
    return current;
}

// Under the mutex. Written beside the file and renamed, so a cut never leaves half a file; the empty string on success.
std::string store(web::Login next) {
    const auto path = file();
    const auto temp = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << web::format_login(next);
        if (!out.flush()) return "cannot write " + temp.string();
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) return "cannot write " + path.string();
    current = std::move(next);
    loaded = true;
    ++changes;
    return {};
}
}  // namespace

std::string set(std::string_view user, std::string_view password) {
    if (!web::valid_user(user)) return "the user name takes 1 to 32 letters, digits, '.', '_' or '-'";
    if (!web::valid_password(password)) return "the password takes 8 to 128 printable characters";
    web::Login next{std::string(user), random_hex(16), iterations, {}, true};
    if (next.salt.empty()) return "no random numbers";
    next.hash = pbkdf2(password, next.salt, next.iterations);
    if (next.hash.empty()) return "hashing failed";
    std::lock_guard lock(mutex);
    return store(std::move(next));
}

std::string set_enabled(bool on) {
    std::lock_guard lock(mutex);
    if (!login()) return "no login";
    web::Login next = *login();
    next.enabled = on;
    return store(std::move(next));
}

void forget() {
    std::lock_guard lock(mutex);
    std::error_code ec;
    std::filesystem::remove(file(), ec);
    current.reset();
    loaded = true;
    ++changes;
}

std::string user() {
    std::lock_guard lock(mutex);
    return login() && login()->enabled ? login()->user : std::string();
}

std::string stored_user() {
    std::lock_guard lock(mutex);
    return login() ? login()->user : std::string();
}

bool enabled() {
    std::lock_guard lock(mutex);
    return login() && login()->enabled;
}

bool verify(std::string_view user, std::string_view password) {
    std::optional<web::Login> copy;
    {
        std::lock_guard lock(mutex);
        copy = login();
    }
    if (!copy || !copy->enabled) return false;
    return web::check(*copy, user, password, pbkdf2);
}

unsigned generation() { return changes.load(); }

std::string random_token() { return random_hex(32); }

}  // namespace prospero::web_login
