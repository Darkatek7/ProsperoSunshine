// SPDX-License-Identifier: GPL-3.0-only
// Development control socket (ROADMAP P2.3). One line per request, one line per answer:
//   STATUS                      -> "OK running=<0|1> app=<name> pending=<n> address=<ip> client=<name>"
//                                  (address and client empty while no stream runs; client is the rest
//                                  of the line and may contain spaces)
//   PAIRINGS                    -> "OK <id> <client name>" per pending request, then "END"
//   PIN <pin> [name]            -> approves the only pending request ("OK paired" / "ERR ...")
//   PIN <id> <pin> [name]       -> approves the given request
//   DEVICES                     -> "OK <uuid> <streaming 0|1> <last mode or -> <name>" per paired device, then "END"
//   STREAMS                     -> per running stream "OK uuid=<uuid> address=<ip> codec=<H.264|HEVC|AV1> hdr=<0|1>
//                                  mode=<WxH@fps> kbps=<n> secs=<n> pads=<n> role=<capture|joined|starting> channel=<0|1|->
//                                  sent_kbps=<n|-> rtt_ms=<n|-> recoveries=<n> name=<name>", then "END" (stream_list.hpp;
//                                  "-" for an unknown value; sent_kbps since the last STREAMS, or since the start)
//   ENDSTREAM <uuid>            -> ends that device's stream; the game and the others' streams go on ("OK ended" / "ERR ...")
//   RENAME <uuid> <name>      -> gives a paired device another name ("OK renamed" / "ERR ...")
//   UNPAIR <uuid>               -> removes a paired device and ends its stream ("OK removed" / "ERR ...")
//   REPORT                      -> "OK port=<n>": for some seconds, <appdata>/diagnostics.txt is on that port
//   AUTOLOAD [ON|OFF]           -> the Payload Manager's autoload list, for the app (autoload_host.hpp)
//   WEBLOGIN                    -> "OK user=<name>", empty without a login (the web page is off then)
//   WEBLOGIN SET <user> <pass>  -> sets the web page's login and turns it on; the password is the rest of the line
//   WEBLOGIN OFF                -> removes the login and turns the web page off
//   QUIT                        -> raises the host's shutdown event
// It has no authentication, so it listens on 127.0.0.1 only (the PS5 app); the empty file
// <appdata>/control_lan opens it to the LAN for development on trusted networks (ROADMAP P9.1).
#pragma once
#include "control/device_modes.hpp"
#include "src/thread_safe.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

namespace prospero {

class ControlServer {
public:
    static constexpr uint16_t port = 47995;
    static constexpr uint16_t report_port = 47996;
    explicit ControlServer(std::shared_ptr<safe::event_t<bool>> shutdown);
    ~ControlServer();
    void run(std::stop_token stop);
    void run_report(std::stop_token stop);
    // Who asks: the app over the socket or the web page. Each has its own STREAMS rate baseline.
    enum class Caller { app, web };
    // Handles one request line; the web page's thread and the socket's call it.
    std::string handle(const std::string &line, Caller caller = Caller::app);
    // Whether RELISTEN was asked for since the last call.
    bool take_relisten() { return relisten_.exchange(false); }

private:
    // The app asks every two seconds while its page is open; this outlasts a missed turn or two.
    static constexpr std::chrono::seconds report_window{8};
    std::shared_ptr<safe::event_t<bool>> shutdown_;
    DeviceModes modes_;
    std::atomic_bool report_listening_{false};
    std::atomic_bool relisten_{false};
    std::atomic<std::chrono::steady_clock::rep> report_until_{0};
    std::string handle_locked(const std::string &line, Caller caller);
    std::mutex mutex_;
    // Each session's video bytes at the caller's last STREAMS, by client certificate: the rate is the difference.
    struct SentSample {
        unsigned long long bytes;
        std::chrono::steady_clock::time_point at;
    };
    std::map<std::string, SentSample> sent_[2];
};

}
