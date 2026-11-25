// SPDX-License-Identifier: GPL-3.0-only
// loss_recovery.hpp against a model of everything around it: an encoder that does what rfi_probe measured (and, where
// nothing is measured, each way it might behave), streams that lose frames on the way, clients that follow Moonlight's
// rules (moonlight-common-c, VideoDepacketizer.c), and the host's loop as video_ps5.cpp runs it. Whatever the losses
// and delays: no client may ever keep a frame that is decoded from a picture it doesn't have, or one with more
// pictures missing in front of it than a decoder counts over, and every client gets its picture back.
// With any argument it prints, per case, how many losses went on without a keyframe.
#include "video/loss_recovery.hpp"
#include <cassert>
#include <cstdio>
#include <deque>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <vector>
using namespace prospero;

namespace {
constexpr int most_missing = 7;

struct Frame {
    uint32_t counter = 0;
    bool keyframe = false;
    int reference = 0;  // distance to the picture it is decoded from
};

// The console's encoder: each frame is decoded from the one before; told that a frame was lost, a frame soon after
// (the next one in the probe) is decoded from the newest picture before the loss; a keyframe two frames after it was
// asked for. Not measured: whether it remembers an invalidation for longer than that one frame (`remembers`), how
// late the effect may come (`late`), and what it does when no picture before the loss is left (`keyframe_when_out`).
struct Encoder {
    bool remembers = false;
    int late = 0;
    bool keyframe_when_out = false;
    static constexpr std::size_t held = 9;

    uint32_t counter = 0;
    std::deque<uint32_t> pictures;
    std::set<uint32_t> invalid;
    std::optional<std::pair<uint32_t, int>> told;  // the frame, and after how many frames it takes effect
    int keyframe_in = 1;  // the first frame is one

    void invalidate(uint32_t frame, std::mt19937 &rng) {
        told = {frame, late ? static_cast<int>(rng() % static_cast<unsigned>(late + 1)) : 0};
    }
    void ask_keyframe() {
        if (keyframe_in < 0) keyframe_in = 2;
    }
    Frame next() {
        Frame frame;
        frame.counter = ++counter;
        bool keyframe = keyframe_in >= 0 && --keyframe_in <= 0;
        uint32_t reference = pictures.empty() ? 0 : pictures.back();
        if (!keyframe && told) {
            if (told->second-- <= 0) {
                if (remembers)
                    for (const uint32_t picture : pictures)
                        if (picture >= told->first) invalid.insert(picture);
                bool found = false;
                for (auto it = pictures.rbegin(); it != pictures.rend() && !found; ++it) {
                    if (remembers ? invalid.count(*it) != 0 : *it >= told->first) continue;
                    reference = *it;
                    found = true;
                }
                if (!found && keyframe_when_out) keyframe = true;
                told.reset();
            }
        } else if (!keyframe && remembers) {
            // Never from a picture it was told is lost.
            for (auto it = pictures.rbegin(); it != pictures.rend(); ++it)
                if (!invalid.count(*it)) {
                    reference = *it;
                    break;
                }
        }
        if (keyframe || pictures.empty()) {
            frame.keyframe = true;
            keyframe_in = -1;
            pictures.clear();
            invalid.clear();
            told.reset();
        } else {
            frame.reference = static_cast<int>(reference) - static_cast<int>(frame.counter);
        }
        pictures.push_back(frame.counter);
        if (pictures.size() > held) pictures.pop_front();
        return frame;
    }
};

struct Packet {
    int64_t index = 0;
    Frame frame;
    bool marked = false;
    int arrives = 0;
};

struct Message {
    bool keyframe = false;  // an IDR request; else the frames first to last are lost
    int64_t first = 0, last = 0;
    int arrives = 0;
};

// Moonlight, as far as going on after a loss goes.
struct Client {
    int64_t next = 1, start = 1;
    bool had_keyframe = false, waits_marked = false, waits_keyframe = false, report = false;
    int dropped_in_a_row = 0, longest_wait = 0, waited = 0;
    std::set<uint32_t> decoded;
    uint32_t last_decoded = 0;
    int went_on_p = 0, went_on_keyframe = 0;
    std::vector<Message> out;

    void lose() {
        if (!had_keyframe || waits_keyframe)
            waits_keyframe = true;
        else
            waits_marked = true;
        if (++dropped_in_a_row == 120) {
            dropped_in_a_row = 0;
            waits_keyframe = true;
            out.push_back({true, 0, 0, 0});
        }
    }
    // False when the client kept a frame it can't decode.
    bool receive(const Packet &packet) {
        if (packet.index > next) {  // the network dropped whole frames
            report = true;
            lose();
        }
        next = packet.index + 1;
        const bool was_waiting = waits_marked || waits_keyframe;
        if (packet.frame.keyframe) {
            waits_marked = waits_keyframe = report = false;
        } else if (packet.marked && waits_marked) {
            waits_marked = report = false;
        }
        if (waits_keyframe) {
            if (report) out.push_back({true, 0, 0, 0});
            report = false;
            lose();
            return true;
        }
        if (waits_marked) {
            out.push_back({false, start, packet.index, 0});
            report = false;
            lose();
            return true;
        }
        if (packet.frame.keyframe) {
            decoded.clear();
            had_keyframe = true;
            went_on_keyframe += was_waiting;
        } else {
            if (!decoded.count(packet.frame.counter + static_cast<uint32_t>(packet.frame.reference))) return false;
            if (packet.frame.counter - last_decoded - 1 > static_cast<uint32_t>(most_missing)) return false;
            went_on_p += was_waiting;
        }
        decoded.insert(packet.frame.counter);
        last_decoded = packet.frame.counter;
        start = next;
        dropped_in_a_row = 0;
        return true;
    }
};

struct Path {
    double loss = 0;
    int burst = 1;  // frames lost in a row
    int down = 0, up = 0;  // delays in frames
};

struct Setup {
    Encoder encoder;
    std::vector<Path> paths;
    int join_at = 0;  // the streams after the first join one by one from this frame on, 0: all at the start
    bool usable = true;
};

struct Totals {
    long went_on_p = 0, went_on_keyframe = 0, keyframes = 0, held = 0, frames = 0;
    int longest_wait = 0;
};

bool run(const Setup &setup, unsigned seed, Totals &totals) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    Encoder encoder = setup.encoder;
    LossRecovery recovery(setup.usable ? most_missing : 0);
    const std::size_t count = setup.paths.size();
    struct Stream {
        int64_t next_index = 1;
        bool started = false, held = false;
        int losing = 0;
        std::deque<Packet> down;
        std::deque<Message> up;
        Client client;
    };
    std::vector<Stream> streams(count);
    const int frames = 3000, quiet_from = 2700;  // nothing is lost at the end, so everyone must be back
    uint32_t counter = 0;
    for (int tick = 0; tick < frames; ++tick) {
        // Between two frames: what the clients said, then the encoder.
        for (std::size_t i = 0; i < count; ++i) {
            Stream &stream = streams[i];
            while (!stream.up.empty() && stream.up.front().arrives <= tick) {
                const Message message = stream.up.front();
                stream.up.pop_front();
                if (message.keyframe || recovery.lost(i + 1, message.first, message.last) == LossRecovery::Loss::keyframe)
                    encoder.ask_keyframe();
            }
            if (stream.held) encoder.ask_keyframe();
        }
        if (counter) {
            const auto call = recovery.call(counter);
            if (!call.keyframe.empty()) encoder.ask_keyframe();
            if (call.invalidate) {
                if (rng() % 97 == 0) {  // the encoder refuses now and then
                    if (!recovery.call_failed().empty()) encoder.ask_keyframe();
                } else {
                    encoder.invalidate(*call.invalidate, rng);
                }
            }
        }
        const Frame frame = encoder.next();
        counter = frame.counter;
        totals.keyframes += frame.keyframe;
        std::optional<hevc::Refs> refs;
        if (!frame.keyframe && recovery.watching(counter)) {
            refs.emplace();
            refs->used.push_back(frame.reference);
        }
        const auto verdict = recovery.frame(counter, frame.keyframe, refs);
        if (!verdict.keyframe.empty()) encoder.ask_keyframe();
        for (const auto id : verdict.held) {
            streams[id - 1].held = true;
            encoder.ask_keyframe();
            ++totals.held;
        }
        for (std::size_t i = 0; i < count; ++i) {
            Stream &stream = streams[i];
            const Path &path = setup.paths[i];
            if (!stream.started) {
                if (i && setup.join_at && tick < setup.join_at * static_cast<int>(i)) continue;
                if (!frame.keyframe) {
                    encoder.ask_keyframe();  // a stream starts at a keyframe
                    continue;
                }
                stream.started = true;
            }
            if (frame.keyframe) stream.held = false;
            if (stream.held) continue;
            Packet packet;
            packet.index = stream.next_index++;
            packet.frame = frame;
            packet.marked = std::find(verdict.recovered.begin(), verdict.recovered.end(), i + 1) != verdict.recovered.end();
            packet.arrives = tick + path.down;
            recovery.sent(i + 1, packet.index, counter);
            if (stream.losing == 0 && tick < quiet_from && chance(rng) < path.loss)
                stream.losing = 1 + static_cast<int>(rng() % static_cast<unsigned>(path.burst));
            if (stream.losing > 0) {
                --stream.losing;
                continue;
            }
            stream.down.push_back(packet);
        }
        // What reaches the clients in this frame's time, and what they say.
        for (std::size_t i = 0; i < count; ++i) {
            Stream &stream = streams[i];
            while (!stream.down.empty() && stream.down.front().arrives <= tick) {
                if (!stream.client.receive(stream.down.front())) {
                    std::printf("seed %u: stream %zu kept frame %u (decoded from %d) without what it needs\n", seed, i,
                                stream.down.front().frame.counter, stream.down.front().frame.reference);
                    return false;
                }
                stream.down.pop_front();
            }
            for (Message message : stream.client.out) {
                message.arrives = tick + 1 + setup.paths[i].up;
                stream.up.push_back(message);
            }
            stream.client.out.clear();
            Client &client = stream.client;
            client.waited = client.waits_marked || client.waits_keyframe ? client.waited + 1 : 0;
            client.longest_wait = std::max(client.longest_wait, client.waited);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Client &client = streams[i].client;
        if (!streams[i].started || client.waits_marked || client.waits_keyframe || streams[i].held || recovery.waiting()) {
            std::printf("seed %u: stream %zu never got its picture back\n", seed, i);
            return false;
        }
        // 120 dropped frames make the client ask for a keyframe itself: a wait that long means the host left it alone.
        if (client.longest_wait >= 150) {
            std::printf("seed %u: stream %zu waited %d frames\n", seed, i, client.longest_wait);
            return false;
        }
        totals.went_on_p += client.went_on_p;
        totals.went_on_keyframe += client.went_on_keyframe;
        totals.longest_wait = std::max(totals.longest_wait, client.longest_wait);
    }
    totals.frames += frames;
    return true;
}
}  // namespace

int main(int argc, char **) {
    struct Case {
        const char *name;
        Setup setup;
        double least_p;  // the least share of the losses that must go on without a keyframe
    };
    const auto encoder = [](bool remembers, int late, bool keyframe_when_out) {
        Encoder e;
        e.remembers = remembers;
        e.late = late;
        e.keyframe_when_out = keyframe_when_out;
        return e;
    };
    const Path lan{0.02, 1, 0, 0}, wifi{0.05, 3, 1, 1}, far{0.03, 2, 2, 3}, bad{0.2, 4, 1, 2}, clean{0.0, 1, 0, 0};
    std::vector<Case> cases;
    for (const bool remembers : {false, true})
        for (const int late : {0, 2})
            for (const bool out : {false, true}) {
                const Encoder e = encoder(remembers, late, out);
                const std::string tag =
                    std::string(remembers ? "remembers" : "forgets") + (late ? ", late" : "") + (out ? ", keyframe when out" : "");
                static std::deque<std::string> names;
                const auto name = [&](const char *what) {
                    return names.emplace_back(std::string(what) + " (" + tag + ")").c_str();
                };
                cases.push_back({name("one stream on a LAN"), {e, {lan}, 0, true}, late ? 0.5 : 0.9});
                cases.push_back({name("one stream on Wi-Fi"), {e, {wifi}, 0, true}, 0.3});
                cases.push_back({name("one stream far away"), {e, {far}, 0, true}, 0.0});
                cases.push_back({name("one stream on a bad path"), {e, {bad}, 0, true}, 0.0});
                cases.push_back({name("two streams, one clean"), {e, {wifi, clean}, 0, true}, 0.3});
                cases.push_back({name("two streams, both lose"), {e, {lan, wifi}, 0, true}, 0.0});
                cases.push_back({name("three streams that join later"), {e, {lan, wifi, far}, 400, true}, 0.0});
                cases.push_back({name("no layout: keyframes only"), {e, {wifi, lan}, 0, false}, 0.0});
            }
    bool ok = true;
    for (const Case &c : cases) {
        Totals totals;
        for (unsigned seed = 1; seed <= 25 && ok; ++seed) ok = run(c.setup, seed, totals);
        if (!ok) {
            std::printf("FAIL %s\n", c.name);
            break;
        }
        const long losses = totals.went_on_p + totals.went_on_keyframe;
        const double share = losses ? static_cast<double>(totals.went_on_p) / static_cast<double>(losses) : 1.0;
        if (argc > 1)
            std::printf("  %-66s %5ld losses, %3.0f%% without a keyframe, %4ld keyframes, %3ld holds, longest wait %d frames\n",
                        c.name, losses, share * 100, totals.keyframes, totals.held, totals.longest_wait);
        if (!c.setup.usable && totals.went_on_p) ok = false;
        if (share < c.least_p) {
            std::printf("FAIL %s: too few losses went on without a keyframe\n", c.name);
            ok = false;
        }
    }
    assert(ok);
    std::cout << "PASS loss_recovery_sim_test\n";
    return 0;
}
