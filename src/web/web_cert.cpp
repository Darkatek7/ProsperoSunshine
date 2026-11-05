// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_cert.hpp"
#include "src/platform/common.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <set>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sys/stat.h>

namespace prospero::web_cert {

namespace {
constexpr long days_valid = 825;      // the most Apple accepts for a server certificate
constexpr long renew_before_days = 30;

template <typename T, void (*Free)(T *)>
struct Deleter {
    void operator()(T *p) const { Free(p); }
};
using X509Ptr = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using KeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;

// The console's IPv4 addresses, loopback included (a local forward such as ps5-tailscale connects there).
std::set<std::string> addresses() {
    std::set<std::string> found{"127.0.0.1"};
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return found;
    for (const ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        char text[INET_ADDRSTRLEN];
        const auto *in = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) found.insert(text);
    }
    freeifaddrs(list);
    return found;
}

std::string san_text(const std::set<std::string> &ips) {
    std::string text = "DNS:localhost";
    for (const auto &ip : ips) text += ",IP:" + ip;
    return text;
}

X509Ptr read_cert(const std::filesystem::path &path) {
    FILE *file = std::fopen(path.c_str(), "r");
    if (!file) return nullptr;
    X509Ptr cert(PEM_read_X509(file, nullptr, nullptr, nullptr));
    std::fclose(file);
    return cert;
}

// Whether the stored certificate can stay: readable, far enough from expiry, and naming every current address.
bool still_good(const std::filesystem::path &cert_path, const std::filesystem::path &key_path, const std::set<std::string> &ips) {
    std::error_code ec;
    if (!std::filesystem::exists(key_path, ec)) return false;
    const X509Ptr cert = read_cert(cert_path);
    if (!cert) return false;
    time_t soon = std::time(nullptr) + renew_before_days * 24 * 3600;
    if (X509_cmp_time(X509_get0_notAfter(cert.get()), &soon) <= 0) return false;
    auto *names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr));
    if (!names) return false;
    std::set<std::string> named;
    for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
        const GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
        if (name->type != GEN_IPADD || ASN1_STRING_length(name->d.iPAddress) != 4) continue;
        char text[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, ASN1_STRING_get0_data(name->d.iPAddress), text, sizeof text)) named.insert(text);
    }
    GENERAL_NAMES_free(names);
    return std::includes(named.begin(), named.end(), ips.begin(), ips.end());
}

bool add_extension(X509 *cert, int nid, const std::string &value) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());
    if (!ext) return false;
    const bool ok = X509_add_ext(cert, ext, -1) == 1;
    X509_EXTENSION_free(ext);
    return ok;
}

// Written beside the target and renamed, so a cut never leaves half a file. The key only for its owner.
bool write_pem(const std::filesystem::path &path, bool key_file, X509 *cert, EVP_PKEY *key) {
    const auto temp = std::filesystem::path(path.string() + ".tmp");
    FILE *file = std::fopen(temp.c_str(), "w");
    if (!file) return false;
    if (key_file) chmod(temp.c_str(), 0600);
    const bool ok = key_file ? PEM_write_PrivateKey(file, key, nullptr, nullptr, 0, nullptr, nullptr) == 1
                             : PEM_write_X509(file, cert) == 1;
    const bool closed = std::fclose(file) == 0;
    std::error_code ec;
    if (ok && closed) std::filesystem::rename(temp, path, ec);
    return ok && closed && !ec;
}

std::string make(const std::filesystem::path &cert_path, const std::filesystem::path &key_path, const std::set<std::string> &ips) {
    KeyPtr key(EVP_EC_gen("P-256"));
    if (!key) return "no key";
    X509Ptr cert(X509_new());
    if (!cert) return "no certificate";
    X509_set_version(cert.get(), 2);
    unsigned char serial[16];
    if (RAND_bytes(serial, sizeof serial) != 1) return "no random numbers";
    serial[0] &= 0x7f;  // positive
    BIGNUM *number = BN_bin2bn(serial, sizeof serial, nullptr);
    if (!number) return "no serial";
    BN_to_ASN1_INTEGER(number, X509_get_serialNumber(cert.get()));
    BN_free(number);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), days_valid * 24 * 3600);
    X509_NAME *name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("ProsperoSunshine web page"),
                               -1, -1, 0);
    X509_set_issuer_name(cert.get(), name);
    X509_set_pubkey(cert.get(), key.get());
    if (!add_extension(cert.get(), NID_basic_constraints, "critical,CA:FALSE") ||
        !add_extension(cert.get(), NID_key_usage, "critical,digitalSignature") ||
        !add_extension(cert.get(), NID_ext_key_usage, "serverAuth") ||
        !add_extension(cert.get(), NID_subject_alt_name, san_text(ips)))
        return "certificate extensions failed";
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0) return "signing failed";
    if (!write_pem(key_path, true, nullptr, key.get()) || !write_pem(cert_path, false, cert.get(), nullptr))
        return "cannot write the certificate files";
    return {};
}
}  // namespace

std::optional<Files> ensure(std::string &problem) {
    const auto cert_path = platf::appdata() / "web_cert.pem";
    const auto key_path = platf::appdata() / "web_key.pem";
    const auto ips = addresses();
    if (!still_good(cert_path, key_path, ips)) {
        problem = make(cert_path, key_path, ips);
        if (!problem.empty()) return std::nullopt;
    }
    return Files{cert_path.string(), key_path.string()};
}

}  // namespace prospero::web_cert
