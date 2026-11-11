// SPDX-License-Identifier: GPL-3.0-only
// The whole read path of the host's HTTP servers: every input is sent over a loopback connection to a Simple-Web-Server
// configured like the GameStream servers, the web page's server, or with nothing set (tests/deps/sws_harness.hpp), and the
// connection is ended in one of three ways. The property: start() has not left, and a plain request on a new connection is
// answered within seconds, so a hung or killed server is a finding like a crash is. (The servers run in this process, on
// their own threads, so a crash in the library is found by the sanitizers and the coverage guides the input.)
// Input: [profile and ending] [how often the second part is repeated, as a power of two] [where to split] [bytes].
#include "fuzz_common.hpp"
#include "sws_harness.hpp"

#include <memory>

using namespace sws_test;

namespace {

struct Servers {
    // Short timeouts: an input that leaves a request half-sent is closed by the target, not by them, but a server that
    // waits a long time to let go would hold the next ones back.
    Instance gamestream{Profile::gamestream, 1, 2};
    Instance page{Profile::page, 1, 2};
    Instance bare{Profile::bare, 1, 2};
};

Servers *servers = nullptr;

}  // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    // The target ignores SIGPIPE through MSG_NOSIGNAL; nothing else to set up. Never freed: the process ends with the run.
    servers = new Servers();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;
    Instance &s = data[0] % 3 == 0 ? servers->gamestream : data[0] % 3 == 1 ? servers->page : servers->bare;
    const int ending = (data[0] / 3) % 3;  // 0: FIN, 1: RST, 2: stays open until it has been quiet for a moment
    const unsigned repeat = data[1] % 12;  // the second part up to 2^11 times: the head limit is 64 KiB
    const std::string payload(reinterpret_cast<const char *>(data) + 3, size - 3);
    const size_t split = payload.empty() ? 0 : data[2] % (payload.size() + 1);
    const std::string first = payload.substr(0, split);
    const std::string second = payload.substr(split);

    {
        auto client = std::make_unique<Client>(s.port());
        // A failed connect() is the machine's (ports in TIME_WAIT, descriptors) unless the server is gone: wait and try again.
        for (int attempt = 0; !client->connected() && attempt < 50; ++attempt) {
            FUZZ_CHECK(!s.ended());
            std::fprintf(stderr, "connect failed (errno %d), retrying\n", client->connect_errno());
            std::this_thread::sleep_for(100ms);
            client = std::make_unique<Client>(s.port());
        }
        Client &c = *client;
        FUZZ_CHECK(c.connected());
        bool open = c.send(first);
        for (unsigned i = 0; i < (1u << repeat) && open && !second.empty() && i * second.size() < (2u << 20); ++i) open = c.send(second);
        if (ending == 1) {
            c.reset();
        } else {
            if (ending == 0) c.finish();
            // What comes back is not looked at beyond reading it; a reply is not part of the property.
            (void)c.read(1000ms, ending == 0 ? 300ms : 5ms, 1 << 16);
        }
    }
    FUZZ_CHECK(!s.ended());
    FUZZ_CHECK(answered(s.port(), 3000ms));
    FUZZ_CHECK(!s.ended());
    return 0;
}
