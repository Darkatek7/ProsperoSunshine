// SPDX-License-Identifier: GPL-3.0-only
// Can the encoder get over a loss without a keyframe (docs/re/encoder-options.md, section 5, live test 1)? Remote Play
// tells it the oldest frame the client lost (sceAvcap2SetInvalidFrame, with the frame counter of the read descriptor);
// what the encoder does then is in libSceVenc and not traced. Each variant opens a fresh channel at 1080p, reads 400
// frames and, at frames 100, 200 and 300, calls SetInvalidFrame as if the last 1, 3 and 8 frames were lost (or asks for a
// keyframe, for the sizes to compare with). It keeps the elementary stream and, per frame, its size and descriptor:
//   /data/prosperosunshine/rfi_probe/<variant>.h264|.h265   the stream, parameter sets first
//   /data/prosperosunshine/rfi_probe/<variant>.csv          n,bytes,type,counter,gop,slices,event,arg,rc,descriptor
//   /data/prosperosunshine/rfi_probe/rfi_probe.log          a line per call and per variant
// tools/rfi_check.py reads them on the PC: it takes the "lost" frames out and looks for the first frame from which the
// decoded pictures are the same again. The picture has to move, or a frame without its reference decodes to the same
// picture anyway: run it with a game or a video in front, not on the home screen. The host must be stopped: a busy
// capture is tried again after 5 s, eight times.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <sys/stat.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
}

namespace {

const char *const dir = "/data/prosperosunshine/rfi_probe";
constexpr int frames_per_run = 400;

struct Field {
    size_t offset;
    uint32_t value;
};

enum class Call { invalidate, keyframe };

struct Variant {
    const char *name;
    uint32_t codec;
    Call call;
    Field fields[3];  // offset 0 = unused
};

// After which frame, and how many of the last frames count as lost.
struct Event {
    int at, lost;
};
const Event events[] = {{100, 1}, {200, 3}, {300, 8}};

// Slices per frame: H.264 NAL types 1 and 5, HEVC VCL types 0..31.
int slices_in(const uint8_t *data, int size, bool hevc) {
    int slices = 0;
    for (int i = 0; i + 3 < size; i++)
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            const int type = hevc ? (data[i + 3] >> 1) & 0x3f : data[i + 3] & 0x1f;
            if (hevc ? type < 32 : type == 1 || type == 5) ++slices;
            i += 2;
        }
    return slices;
}

uint32_t read_u32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

int run(std::vector<uint8_t> &buffer, const Variant &v) {
    const bool hevc = v.codec == 1;
    // ReadVideo2's descriptor is ReadVideo's, 0x10 further back (docs/avcap2-descriptor.md).
    const size_t base = hevc ? 0x10 : 0, desc_size = hevc ? 0x88 : 0x70;
    ps_capture_set_codec(v.codec);
    ps_capture_set_ten_bit(0);
    ps_capture_set_video_mode(4, 1);
    for (int i = 0; i < 3; i++) ps_capture_override_extra(i, v.fields[i].offset, v.fields[i].value);
    ps_capture *s = nullptr;
    const int open_rc = ps_capture_open(&s);
    for (int i = 0; i < 3; i++) ps_capture_override_extra(i, 0, 0);
    if (open_rc) {
        ps_diag_log("ProsperoSunshine: rfi %s: failed %d raw=%#x\n", v.name, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
        usleep(500000);
        return open_rc;
    }
    char path[160];
    snprintf(path, sizeof path, "%s/%s.%s", dir, v.name, hevc ? "h265" : "h264");
    FILE *out = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/%s.csv", dir, v.name);
    FILE *csv = fopen(path, "w");
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (out && header_size > 0) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
    if (csv)
        fprintf(csv, "# headers=%d codec=%s\nn,bytes,type,counter,gop,slices,event,arg,rc,descriptor\n",
                header_size > 0 ? header_size : 0, hevc ? "hevc" : "h264");
    uint8_t desc[0x88];
    std::vector<uint32_t> counters;
    int frames = 0, error = 0, idrs = 0;
    long bytes = 0;
    const auto begin = std::chrono::steady_clock::now();
    while (frames < frames_per_run && std::chrono::steady_clock::now() - begin < std::chrono::seconds(20)) {
        memset(desc, 0, sizeof desc);
        const int rc =
            hevc ? ps_capture_read2(s, buffer.data(), buffer.size(), desc) : ps_capture_read(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) {
            error = rc;
            break;
        }
        if (rc == 0) {
            usleep(500);
            continue;
        }
        ++frames;
        bytes += rc;
        if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        const uint32_t type = read_u32(desc + base + 0x1c), counter = read_u32(desc + base + 0x20),
                       gop = read_u32(desc + base + 0x28);
        counters.push_back(counter);
        idrs += type == 1;
        const char *event = "";
        uint32_t arg = 0;
        int call_rc = 0;
        for (const Event &e : events) {
            if (e.at != frames) continue;
            if (v.call == Call::keyframe) {
                event = "keyframe";
                call_rc = ps_capture_request_keyframe(s);
            } else {
                // The oldest frame the client lost, as Remote Play passes it.
                event = "invalidate";
                arg = counters[counters.size() - static_cast<size_t>(e.lost)];
                call_rc = ps_capture_invalidate_frame(s, arg);
            }
            ps_diag_log("ProsperoSunshine: rfi %s: frame %d counter %u: %s(%u) rc=%#x\n", v.name, frames, counter, event, arg,
                        static_cast<unsigned>(call_rc));
        }
        if (csv) {
            fprintf(csv, "%d,%d,%u,%u,%u,%d,%s,%u,%#x,", frames, rc, type, counter, gop, slices_in(buffer.data(), rc, hevc), event,
                    arg, static_cast<unsigned>(call_rc));
            for (size_t i = 0; i < desc_size; i++) fprintf(csv, "%02x", desc[i]);
            fputc('\n', csv);
        }
    }
    if (out) fclose(out);
    if (csv) fclose(csv);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const int close_rc = ps_capture_close(s);
    ps_diag_log("ProsperoSunshine: rfi %s: ok frames=%d idr=%d fps=%.1f kbit/s=%.0f err=%#x close=%d\n", v.name, frames, idrs,
                frames / seconds, bytes * 8 / seconds / 1000, static_cast<unsigned>(error), close_rc);
    usleep(500000);
    return 0;
}

}  // namespace

int main() {
    mkdir(dir, 0777);
    char header[96];
    snprintf(header, sizeof header, "ProsperoSunshine: rfi_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/rfi_probe/rfi_probe.log", header);
    ps_capture_set_opening_bitrate(16000000, 17600000);
    std::vector<uint8_t> buffer(16 * 1024 * 1024);
    // ref2: the record's slot +0x08 is a fixed slice count or the number of reference frames (encoder-options.md, section
    // 3); 2 tells which, and whether a second reference changes what SetInvalidFrame does. ltr: the long-term reference
    // block as section 5 reads it (size 0x50, on, one frame).
    static const Variant variants[] = {
        {"h264_key", 0, Call::keyframe, {}},
        {"h264_rfi", 0, Call::invalidate, {}},
        {"h264_rfi_ref2", 0, Call::invalidate, {{0x190, 1}, {0x194, 2}}},
        {"h264_rfi_ltr", 0, Call::invalidate, {{0x138, 0x50}, {0x140, 1}, {0x144, 1}}},
        {"hevc_key", 1, Call::keyframe, {}},
        {"hevc_rfi", 1, Call::invalidate, {}},
        {"hevc_rfi_ref2", 1, Call::invalidate, {{0x190, 1}, {0x194, 2}}},
        {"hevc_rfi_ltr", 1, Call::invalidate, {{0x138, 0x50}, {0x140, 1}, {0x144, 1}}},
        {"h264_rfi_again", 0, Call::invalidate, {}},
    };
    for (const auto &v : variants)
        for (int busy = 0; run(buffer, v) == PS_CAPTURE_BUSY;) {
            if (++busy == 8) {
                ps_diag_log("ProsperoSunshine: rfi capture busy eight times, stopping\n");
                return 1;
            }
            sleep(5);
        }
    ps_diag_log("ProsperoSunshine: rfi done\n");
    return 0;
}
