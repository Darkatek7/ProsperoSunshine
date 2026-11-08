// SPDX-License-Identifier: GPL-3.0-only
// M0: exercise the cross-built Boost libraries the way Sunshine uses them.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
extern "C" {
#include <ps5/klog.h>
}

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = asio::ip::tcp;

namespace {

void note(const char *what, const std::string &detail) {
    klog_printf("ProsperoSunshine: boost %s: %s\n", what, detail.c_str());
}

// ---- 1. Asio TCP + TLS over loopback ----

// Self-signed RSA-2048 certificate, as Sunshine creates for its HTTPS pairing server.
bool make_identity(EVP_PKEY **key_out, X509 **cert_out) {
    EVP_PKEY *key = EVP_RSA_gen(2048);
    if (!key) return false;
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
    if (!X509_sign(cert, key, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(key);
        return false;
    }
    *key_out = key;
    *cert_out = cert;
    return true;
}

using tls_socket = ssl::stream<tcp::socket>;
using error_code = boost::system::error_code;

// One accepted connection: handshake, read a message, answer "pong:<message>".
struct ServerSession : std::enable_shared_from_this<ServerSession> {
    tls_socket socket;
    std::array<char, 128> buf{};
    std::string reply;
    std::promise<std::string> *done;

    ServerSession(asio::io_context &io, ssl::context &ctx, std::promise<std::string> *p) : socket(io, ctx), done(p) {}

    void start() {
        auto self = shared_from_this();
        socket.async_handshake(ssl::stream_base::server, [self](const error_code &ec) {
            if (ec) return self->done->set_value("server handshake: " + ec.message());
            self->socket.async_read_some(asio::buffer(self->buf), [self](const error_code &ec2, std::size_t n) {
                if (ec2) return self->done->set_value("server read: " + ec2.message());
                self->reply = "pong:" + std::string(self->buf.data(), n);
                asio::async_write(self->socket, asio::buffer(self->reply), [self](const error_code &ec3, std::size_t) {
                    self->done->set_value(ec3 ? "server write: " + ec3.message() : "");
                });
            });
        });
    }
};

struct ClientSession : std::enable_shared_from_this<ClientSession> {
    tls_socket socket;
    std::string request = "ping from the PS5";
    std::array<char, 128> buf{};
    std::promise<std::string> *done;
    std::string version;

    ClientSession(asio::io_context &io, ssl::context &ctx, std::promise<std::string> *p) : socket(io, ctx), done(p) {}

    void start(const tcp::endpoint &ep) {
        auto self = shared_from_this();
        socket.lowest_layer().async_connect(ep, [self](const error_code &ec) {
            if (ec) return self->done->set_value("client connect: " + ec.message());
            self->socket.async_handshake(ssl::stream_base::client, [self](const error_code &ec2) {
                if (ec2) return self->done->set_value("client handshake: " + ec2.message());
                self->version = SSL_get_version(self->socket.native_handle());
                asio::async_write(self->socket, asio::buffer(self->request), [self](const error_code &ec3, std::size_t) {
                    if (ec3) return self->done->set_value("client write: " + ec3.message());
                    self->socket.async_read_some(asio::buffer(self->buf), [self](const error_code &ec4, std::size_t n) {
                        if (ec4) return self->done->set_value("client read: " + ec4.message());
                        self->done->set_value(std::string(self->buf.data(), n));
                    });
                });
            });
        });
    }
};

bool asio_tls_probe() {
    EVP_PKEY *key = nullptr;
    X509 *cert = nullptr;
    if (!make_identity(&key, &cert)) return note("asio_tls", "certificate generation failed"), false;
    bool ok = false;
    try {
        asio::io_context io;
        ssl::context sctx(ssl::context::tls_server);
        ssl::context cctx(ssl::context::tls_client);
        if (SSL_CTX_use_certificate(sctx.native_handle(), cert) != 1 ||
            SSL_CTX_use_PrivateKey(sctx.native_handle(), key) != 1)
            throw std::runtime_error("loading identity into the server context failed");
        cctx.set_verify_mode(ssl::verify_none);  // Moonlight pins the certificate, it does not use a CA

        tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        const tcp::endpoint ep = acceptor.local_endpoint();
        std::promise<std::string> server_done, client_done;
        auto server = std::make_shared<ServerSession>(io, sctx, &server_done);
        auto client = std::make_shared<ClientSession>(io, cctx, &client_done);
        acceptor.async_accept(server->socket.lowest_layer(), [&](const error_code &ec) {
            if (ec) server_done.set_value("accept: " + ec.message());
            else server->start();
        });
        client->start(ep);

        std::future<std::string> sf = server_done.get_future(), cf = client_done.get_future();
        const auto start = std::chrono::steady_clock::now();
        {
            std::jthread runner([&io] { io.run(); });
            const auto limit = std::chrono::seconds(20);
            const bool got_c = cf.wait_for(limit) == std::future_status::ready;
            const bool got_s = got_c && sf.wait_for(limit) == std::future_status::ready;
            if (!got_c || !got_s) {
                note("asio_tls", "timed out");
            } else {
                const std::string s = sf.get(), c = cf.get();
                ok = s.empty() && c == "pong:ping from the PS5";
                if (!ok) note("asio_tls", "server=\"" + s + "\" client=\"" + c + "\"");
            }
            io.stop();
        }  // jthread joins here
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        klog_printf("ProsperoSunshine: boost asio_tls port=%u version=%s elapsed_ms=%lld ok=%d\n",
                    static_cast<unsigned>(ep.port()), client->version.c_str(), static_cast<long long>(ms.count()), ok);
    } catch (const std::exception &e) {
        note("asio_tls exception", e.what());
        ok = false;
    }
    X509_free(cert);
    EVP_PKEY_free(key);
    return ok;
}

// ---- 2. Boost.Log -> klog ----

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;

std::atomic<int> g_klog_records{0};
std::string g_last_record;

// Sunshine's own sink forwards formatted records to its platform log; here the platform log is klog.
struct KlogBackend : sinks::basic_formatted_sink_backend<char, sinks::synchronized_feeding> {
    void consume(const logging::record_view &, const std::string &text) {
        klog_printf("ProsperoSunshine: boost log: %s\n", text.c_str());
        g_last_record = text;
        ++g_klog_records;
    }
};

bool log_probe() {
    try {
        using sink_t = sinks::synchronous_sink<KlogBackend>;
        auto sink = boost::make_shared<sink_t>();
        sink->set_formatter(expr::stream << "[" << expr::attr<int>("Severity") << "] " << expr::smessage);
        logging::core::get()->add_sink(sink);
        logging::add_common_attributes();
        logging::sources::severity_logger<int> lg;
        BOOST_LOG_SEV(lg, 2) << "hello from Boost.Log " << 42;
        sink->flush();
        logging::core::get()->remove_sink(sink);
        const bool ok = g_klog_records == 1 && g_last_record == "[2] hello from Boost.Log 42";
        if (!ok) note("log", "records=" + std::to_string(g_klog_records.load()) + " last=\"" + g_last_record + "\"");
        return ok;
    } catch (const std::exception &e) {
        note("log exception", e.what());
        return false;
    }
}

// ---- 3. property_tree ----

namespace pt = boost::property_tree;

bool ptree_probe() {
    try {
        pt::ptree tree;
        tree.put("root.name", "ProsperoSunshine");
        tree.put("root.port", 47989);
        tree.put("root.enabled", true);
        tree.put("root.ratio", 0.5);
        pt::ptree apps;
        for (const char *app : {"Desktop", "Steam", "Game"}) {
            pt::ptree node;
            node.put("title", app);
            apps.push_back({"app", node});
        }
        tree.add_child("root.apps", apps);

        std::ostringstream xml_out, json_out;
        pt::write_xml(xml_out, tree);
        pt::write_json(json_out, tree);

        pt::ptree from_xml, from_json;
        std::istringstream xml_in(xml_out.str()), json_in(json_out.str());
        pt::read_xml(xml_in, from_xml);
        pt::read_json(json_in, from_json);

        auto check = [](const pt::ptree &t) {
            return t.get<std::string>("root.name") == "ProsperoSunshine" && t.get<int>("root.port") == 47989 &&
                   t.get<bool>("root.enabled") && t.get<double>("root.ratio") == 0.5 &&
                   t.get_child("root.apps").size() == 3 &&
                   t.get_child("root.apps").back().second.get<std::string>("title") == "Game";
        };
        // A second serialisation must be identical: nothing was lost or reordered.
        std::ostringstream xml_again, json_again;
        pt::write_xml(xml_again, from_xml);
        pt::write_json(json_again, from_json);
        const bool ok = check(from_xml) && check(from_json) && xml_again.str() == xml_out.str() &&
                        json_again.str() == json_out.str();
        klog_printf("ProsperoSunshine: boost ptree xml_bytes=%zu json_bytes=%zu ok=%d\n", xml_out.str().size(),
                    json_out.str().size(), ok);
        return ok;
    } catch (const std::exception &e) {
        note("ptree exception", e.what());
        return false;
    }
}

// ---- 4. Filesystem ----

namespace fs = boost::filesystem;

bool fs_probe() {
    boost::system::error_code ec;
    const fs::path dir = "/data/prosperosunshine-probe";
    const fs::path file = dir / "probe.txt";
    const std::string payload = "ProsperoSunshine filesystem probe\n";
    bool ok = false;
    try {
        fs::create_directories(dir, ec);
        if (ec) return note("fs", "create_directories: " + ec.message()), false;
        {
            std::ofstream out(file.string(), std::ios::binary);
            out << payload;
        }
        std::ifstream in(file.string(), std::ios::binary);
        const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const auto size = fs::file_size(file, ec);
        std::size_t entries = 0;
        for (const auto &entry : fs::directory_iterator(dir)) entries += entry.path().filename() == "probe.txt";
        ok = back == payload && !ec && size == payload.size() && entries == 1 && fs::exists(file);
        if (!ok) note("fs", "readback mismatch or bad size");
    } catch (const std::exception &e) {
        note("fs exception", e.what());
    }
    fs::remove_all(dir, ec);
    const bool gone = !fs::exists(dir);
    if (!gone) note("fs", "remove_all left the directory behind");
    klog_printf("ProsperoSunshine: boost fs dir=%s roundtrip=%d removed=%d\n", dir.c_str(), ok, gone);
    return ok && gone;
}

// ---- 5. endian ----

namespace en = boost::endian;

bool endian_probe() {
    en::big_uint32_t be = 0x01020304u;
    en::little_uint32_t le = 0x01020304u;
    unsigned char be_bytes[4], le_bytes[4];
    std::memcpy(be_bytes, &be, 4);
    std::memcpy(le_bytes, &le, 4);
    const bool layout = be_bytes[0] == 1 && be_bytes[3] == 4 && le_bytes[0] == 4 && le_bytes[3] == 1;

    be += 1;  // arithmetic types: the in-memory form stays big endian while the value changes
    le *= 2;
    const bool arithmetic = be == 0x01020305u && le == 0x02040608u;

    en::big_int16_t neg = -2;
    const bool sign = neg == -2 && static_cast<int>(neg) + 1 == -1;

    const unsigned char wire[8] = {0x00, 0x00, 0x01, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    const bool load = en::load_big_u32(wire) == 256u && en::load_little_u32(wire + 4) == 0xEFBEADDEu;
    const bool reverse = en::endian_reverse(std::uint32_t{0x11223344u}) == 0x44332211u &&
                         en::big_to_native(en::native_to_big(std::uint64_t{0x0102030405060708ull})) ==
                             0x0102030405060708ull;
    const bool ok = layout && arithmetic && sign && load && reverse;
    klog_printf("ProsperoSunshine: boost endian layout=%d arithmetic=%d sign=%d load=%d reverse=%d\n", layout,
                arithmetic, sign, load, reverse);
    return ok;
}

}  // namespace

int main() {
    const bool asio_tls = asio_tls_probe();
    const bool log = log_probe();
    const bool ptree = ptree_probe();
    const bool fsok = fs_probe();
    const bool endian = endian_probe();
    const bool ok = asio_tls && log && ptree && fsok && endian;
    klog_printf("ProsperoSunshine: boost result=%s asio_tls=%d log=%d ptree=%d fs=%d endian=%d\n",
                ok ? "PASS" : "FAIL", asio_tls, log, ptree, fsok, endian);
    return ok ? 0 : 1;
}
