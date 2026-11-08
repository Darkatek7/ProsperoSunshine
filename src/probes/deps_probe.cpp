// SPDX-License-Identifier: GPL-3.0-only
// M0: exercise the cross-built third-party libraries the way Sunshine uses them.
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <enet/enet.h>
#include <nanors/rs.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <opus/opus_multistream.h>
extern "C" {
#include <ps5/klog.h>
}

namespace {

using clock_type = std::chrono::steady_clock;

long long elapsed_us(clock_type::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - start).count();
}

void log_openssl_error(const char *what) {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof buf);
    klog_printf("ProsperoSunshine: deps openssl %s failed: %s\n", what, buf);
}

// Self-signed RSA-2048 certificate, as Sunshine creates for pairing.
bool make_identity(EVP_PKEY **key_out, X509 **cert_out) {
    EVP_PKEY *key = EVP_RSA_gen(2048);
    if (!key) return log_openssl_error("rsa keygen"), false;
    X509 *cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 365 * 20);
    X509_set_pubkey(cert, key);
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("ProsperoSunshine"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    if (!X509_sign(cert, key, EVP_sha256()) || X509_verify(cert, key) != 1) {
        log_openssl_error("x509 sign/verify");
        X509_free(cert);
        EVP_PKEY_free(key);
        return false;
    }
    *key_out = key;
    *cert_out = cert;
    return true;
}

// TLS 1.3 handshake and one record each way over an in-memory BIO pair.
bool tls_roundtrip(EVP_PKEY *key, X509 *cert) {
    SSL_CTX *sctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX *cctx = SSL_CTX_new(TLS_client_method());
    bool ok = sctx && cctx && SSL_CTX_use_certificate(sctx, cert) == 1 && SSL_CTX_use_PrivateKey(sctx, key) == 1;
    SSL *server = ok ? SSL_new(sctx) : nullptr;
    SSL *client = ok ? SSL_new(cctx) : nullptr;
    if (server && client) {
        BIO *sbio = nullptr, *cbio = nullptr;
        BIO_new_bio_pair(&sbio, 0, &cbio, 0);
        SSL_set_bio(server, sbio, sbio);
        SSL_set_bio(client, cbio, cbio);
        SSL_set_accept_state(server);
        SSL_set_connect_state(client);
        bool sdone = false, cdone = false;
        for (int i = 0; i < 100 && !(sdone && cdone); ++i) {
            if (!cdone) cdone = SSL_do_handshake(client) == 1;
            if (!sdone) sdone = SSL_do_handshake(server) == 1;
        }
        char reply[8] = {};
        ok = sdone && cdone && SSL_write(client, "ping", 4) == 4 && SSL_read(server, reply, sizeof reply) == 4 &&
             std::memcmp(reply, "ping", 4) == 0;
        klog_printf("ProsperoSunshine: deps tls handshake=%d version=%s cipher=%s\n", sdone && cdone,
                    SSL_get_version(client), SSL_get_cipher_name(client));
    } else {
        ok = false;
    }
    if (!ok) log_openssl_error("tls");
    SSL_free(client);
    SSL_free(server);
    SSL_CTX_free(cctx);
    SSL_CTX_free(sctx);
    return ok;
}

// AES-128-GCM round trip and throughput; Moonlight encrypts video and control with it.
bool aes_gcm(double *mib_per_s) {
    std::vector<unsigned char> plain(1 << 20), cipher(plain.size()), back(plain.size());
    unsigned char key[16], iv[12], tag[16];
    if (RAND_bytes(key, sizeof key) != 1 || RAND_bytes(iv, sizeof iv) != 1 ||
        RAND_bytes(plain.data(), static_cast<int>(plain.size())) != 1)
        return log_openssl_error("rand"), false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    constexpr int rounds = 64;
    const auto start = clock_type::now();
    for (int i = 0; i < rounds; ++i) {
        EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, key, iv);
        EVP_EncryptUpdate(ctx, cipher.data(), &len, plain.data(), static_cast<int>(plain.size()));
        EVP_EncryptFinal_ex(ctx, cipher.data() + len, &len);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof tag, tag);
    }
    *mib_per_s = rounds / (elapsed_us(start) / 1e6);
    EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, key, iv);
    EVP_DecryptUpdate(ctx, back.data(), &len, cipher.data(), static_cast<int>(cipher.size()));
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof tag, tag);
    const bool authentic = EVP_DecryptFinal_ex(ctx, back.data() + len, &len) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return authentic && back == plain;
}

bool openssl_probe() {
    EVP_PKEY *key = nullptr;
    X509 *cert = nullptr;
    const auto start = clock_type::now();
    if (!make_identity(&key, &cert)) return false;
    const long long keygen_ms = elapsed_us(start) / 1000;
    const bool tls = tls_roundtrip(key, cert);
    double mib_per_s = 0;
    const bool gcm = aes_gcm(&mib_per_s);
    klog_printf("ProsperoSunshine: deps openssl version=\"%s\" keygen_ms=%lld tls=%d aes_gcm=%d aes_gcm_mib_s=%.0f\n",
                OpenSSL_version(OPENSSL_VERSION), keygen_ms, tls, gcm, mib_per_s);
    X509_free(cert);
    EVP_PKEY_free(key);
    return tls && gcm;
}

// Stereo 48 kHz multistream with 5 ms packets, Sunshine's default audio configuration.
bool opus_probe() {
    constexpr int rate = 48000, channels = 2, frame = rate / 200, packets = 400;
    const unsigned char mapping[channels] = {0, 1};
    int err = 0;
    auto *enc = opus_multistream_encoder_create(rate, channels, 1, 1, mapping,
                                                OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    auto *dec = enc ? opus_multistream_decoder_create(rate, channels, 1, 1, mapping, &err) : nullptr;
    if (!enc || !dec) {
        klog_printf("ProsperoSunshine: deps opus create failed: %s\n", opus_strerror(err));
        return false;
    }
    opus_multistream_encoder_ctl(enc, OPUS_SET_BITRATE(96000));
    std::vector<float> pcm(frame * channels), out(frame * channels);
    unsigned char packet[1400];
    double energy = 0;
    long long encode_us = 0;
    bool ok = true;
    for (int p = 0; p < packets && ok; ++p) {
        for (int i = 0; i < frame; ++i) {
            const float s = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * (p * frame + i) / rate);
            pcm[i * 2] = pcm[i * 2 + 1] = s;
        }
        const auto start = clock_type::now();
        const int bytes = opus_multistream_encode_float(enc, pcm.data(), frame, packet, sizeof packet);
        encode_us += elapsed_us(start);
        const int samples = bytes > 0 ? opus_multistream_decode_float(dec, packet, bytes, out.data(), frame, 0) : bytes;
        ok = samples == frame;
        if (p >= packets / 2)
            for (float v : out) energy += static_cast<double>(v) * v;
    }
    const double rms = std::sqrt(energy / (packets / 2 * frame * channels));
    klog_printf("ProsperoSunshine: deps opus version=\"%s\" packets=%d rms=%.3f encode_us_per_packet=%lld\n",
                opus_get_version_string(), packets, rms, encode_us / packets);
    opus_multistream_decoder_destroy(dec);
    opus_multistream_encoder_destroy(enc);
    return ok && rms > 0.1 && rms < 0.25;
}

// Loopback connect plus one reliable packet, the control-stream transport.
bool enet_probe() {
    if (enet_initialize() != 0) return false;
    ENetAddress addr{};
    enet_address_set_host(&addr, "127.0.0.1");
    enet_address_set_port(&addr, 47999);
    ENetHost *server = enet_host_create(AF_INET, &addr, 1, 0, 0, 0);
    ENetHost *client = server ? enet_host_create(AF_INET, nullptr, 1, 0, 0, 0) : nullptr;
    ENetPeer *peer = client ? enet_host_connect(client, &addr, 1, 0) : nullptr;
    bool connected = false, received = false;
    const auto start = clock_type::now();
    for (int i = 0; i < 400 && peer && !received; ++i) {
        ENetEvent event;
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
                enet_peer_send(peer, 0, enet_packet_create("ctrl", 4, ENET_PACKET_FLAG_RELIABLE));
            }
        }
        while (enet_host_service(server, &event, 5) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                received = event.packet->dataLength == 4 && std::memcmp(event.packet->data, "ctrl", 4) == 0;
                enet_packet_destroy(event.packet);
            }
        }
    }
    klog_printf("ProsperoSunshine: deps enet server=%d connected=%d received=%d elapsed_ms=%lld\n",
                server != nullptr, connected, received, elapsed_us(start) / 1000);
    if (client) enet_host_destroy(client);
    if (server) enet_host_destroy(server);
    enet_deinitialize();
    return connected && received;
}

// Reed-Solomon FEC: lose as many shards as there is parity and rebuild them.
bool nanors_probe() {
    constexpr int data = 20, parity = 10, total = data + parity, block = 1400;
    reed_solomon_init();
    reed_solomon *rs = reed_solomon_new(data, parity);
    if (!rs) return false;
    std::vector<std::vector<uint8_t>> store(total, std::vector<uint8_t>(block));
    std::vector<uint8_t *> shards(total);
    for (int i = 0; i < total; ++i) shards[i] = store[i].data();
    if (RAND_bytes(store[0].data(), block) != 1) return reed_solomon_release(rs), false;
    for (int i = 1; i < data; ++i)
        for (int j = 0; j < block; ++j) store[i][j] = static_cast<uint8_t>(store[0][j] * 31 + i * 7 + j);
    const auto original = store;
    const auto start = clock_type::now();
    int rc = reed_solomon_encode(rs, shards.data(), total, block);
    const long long encode_us = elapsed_us(start);
    std::vector<uint8_t> marks(total, 0);
    for (int i = 0; i < parity; ++i) {
        const int lost = (i * 3) % total;
        marks[lost] = 1;
        std::memset(store[lost].data(), 0, block);
    }
    if (rc == 0) rc = reed_solomon_decode(rs, shards.data(), marks.data(), total, block);
    bool same = true;
    for (int i = 0; i < data; ++i) same = same && store[i] == original[i];
    // align_size reveals the SIMD kernel nanors picked: 32 means AVX2, 16 SSSE3, 8 the scalar fallback.
    klog_printf("ProsperoSunshine: deps nanors encode_rc=%d recovered=%d encode_us=%lld simd_align=%zu\n", rc, same,
                encode_us, rs->align_size);
    reed_solomon_release(rs);
    return rc == 0 && same;
}

}  // namespace

int main() {
    const bool ssl = openssl_probe();
    const bool opus = opus_probe();
    const bool enet = enet_probe();
    const bool fec = nanors_probe();
    const bool ok = ssl && opus && enet && fec;
    klog_printf("ProsperoSunshine: deps result=%s openssl=%d opus=%d enet=%d nanors=%d\n",
                ok ? "PASS" : "FAIL", ssl, opus, enet, fec);
    return ok ? 0 : 1;
}
