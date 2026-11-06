// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_login.hpp"
#include "web/web_auth.hpp"
#include "src/platform/common.h"
#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace prospero::web_login {

namespace {
// PBKDF2-SHA256; about a tenth of a second on the console, once per login.
constexpr unsigned iterations = 100000;

std::mutex mutex;
std::atomic<unsigned> changes{0};
bool loaded = false;
std::optional<web::Login> current;

std::filesystem::path file() { return platf::appdata() / "web_login"; }
std::filesystem::path remembered_file() { return platf::appdata() / "web_remembered"; }

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

// Under the mutex: every remembered browser has to sign in again.
void drop_remembered() {
    std::error_code ec;
    std::filesystem::remove(remembered_file(), ec);
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
    drop_remembered();
    return store(std::move(next));
}

std::string set_enabled(bool on) {
    std::lock_guard lock(mutex);
    if (!login()) return "no login";
    web::Login next = *login();
    next.enabled = on;
    if (!on) drop_remembered();
    return store(std::move(next));
}

void forget() {
    std::lock_guard lock(mutex);
    std::error_code ec;
    std::filesystem::remove(file(), ec);
    drop_remembered();
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

std::string token_hash(std::string_view token) {
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(token.data()), token.size(), out);
    return hex(out, sizeof out);
}

long long wall_now() { return static_cast<long long>(std::time(nullptr)); }

web::Remembered load_remembered() {
    std::lock_guard lock(mutex);
    if (!login() || !login()->enabled) return {};
    std::ostringstream text;
    text << std::ifstream(remembered_file(), std::ios::binary).rdbuf();
    return web::parse_remembered(text.str(), web::login_id(*login(), token_hash), wall_now());
}

std::string save_remembered(web::Remembered remembered, unsigned generation) {
    std::lock_guard lock(mutex);
    // A login changed meanwhile has dropped them already; they must not come back.
    if (changes.load() != generation || !login() || !login()->enabled) return "the login changed";
    if (remembered.size() == 0) {
        drop_remembered();
        return {};
    }
    remembered.login_id = web::login_id(*login(), token_hash);
    const std::string text = web::format_remembered(remembered);
    const auto path = remembered_file();
    const std::string temp = path.string() + ".tmp";
    // Only for the host's own user, as the certificate's key.
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return "cannot write " + temp;
    fchmod(fd, 0600);
    bool ok = true;
    for (size_t done = 0; ok && done < text.size();) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n <= 0) ok = false;
        else done += static_cast<size_t>(n);
    }
    if (::close(fd) != 0) ok = false;
    std::error_code ec;
    if (ok) std::filesystem::rename(temp, path, ec);
    if (!ok || ec) {
        std::filesystem::remove(temp, ec);
        return "cannot write " + path.string();
    }
    return {};
}

}  // namespace prospero::web_login
