// SPDX-License-Identifier: GPL-3.0-only
// PC tests for the app's pure parts: sunshine.conf edits, the Payload Manager's autoload list, the
// diagnostic report, the web page's login and (settings_layout_test.cpp) the settings page's layout.
#include "app/psapp/autoload.hpp"
#include "app/psapp/conf.hpp"
#include "app/psapp/diag.hpp"
#include "app/psapp/experimental_list.h"
#include "app/psapp/web.hpp"

#include <cstdio>
#include <string>

#include "check.hpp"

// settings_layout_test.cpp
void settings_layout_tests();
bool write_settings_svg(const char *path);

using namespace psapp;

static void conf_tests()
{
    // An empty file: every key has its default.
    Config empty = Config::parse("");
    CHECK(!empty.get("port"));
    empty.set("port", "48000");
    CHECK(empty.text() == "port = 48000\n");

    // Comments, unknown keys and spacing stay; a known key is replaced in place.
    const std::string text = "# my settings\r\nsunshine_name=Living room # tv\nfoo = bar\n\nport = 47989\n";
    Config c = Config::parse(text);
    CHECK(c.get("sunshine_name") == std::optional<std::string>("Living room"));
    CHECK(c.get("foo") == std::optional<std::string>("bar"));
    c.set("sunshine_name", "PS5");
    c.set("min_log_level", "debug");
    c.set("port", std::nullopt);
    CHECK(c.text() == "# my settings\nsunshine_name = PS5\nfoo = bar\n\nmin_log_level = debug\n");

    // Sunshine keeps a key's first line; set() keeps that place and drops the later duplicates.
    Config d = Config::parse("ping_timeout = 1000\nx = 1\nping_timeout = 3000\n");
    CHECK(d.get("ping_timeout") == std::optional<std::string>("1000"));
    d.set("ping_timeout", "5000");
    CHECK(d.text() == "ping_timeout = 5000\nx = 1\n");

    // A line without a value is no setting.
    CHECK(!Config::parse("port =\n").get("port"));

    const OptionSpec *port = find_option("port");
    REQUIRE(port != nullptr);
    CHECK(!validate(*port, "47989"));
    CHECK(validate(*port, "1024"));
    CHECK(validate(*port, "65515"));
    CHECK(validate(*port, "12a"));
    const OptionSpec *name = find_option("sunshine_name");
    CHECK(!validate(*name, "My PS5"));
    CHECK(validate(*name, "a#b"));
    CHECK(validate(*name, " lead"));
    CHECK(validate(*name, ""));
    const OptionSpec *level = find_option("min_log_level");
    CHECK(!validate(*level, "warning"));
    CHECK(validate(*level, "loud"));
    CHECK(!validate(*find_option("lan_encryption_mode"), "2"));
    CHECK(validate(*find_option("wan_encryption_mode"), "3"));
    CHECK(!validate(*find_option("ping_timeout"), "2000"));
    CHECK(validate(*find_option("ping_timeout"), "100"));

    // close_game_on_quit: off by default, the app writes 1, and Sunshine's other words for on show as on.
    const OptionSpec *quit = find_option("close_game_on_quit");
    REQUIRE(quit != nullptr);
    CHECK(quit->kind == OptionKind::toggle);
    CHECK(!validate(*quit, "1"));
    CHECK(!option_value(Config::parse("port = 48000\n"), *quit));
    for (const char *on : {"1", "enabled", "enable", "true", "yes", "on"})
        CHECK(option_value(Config::parse(std::string("close_game_on_quit = ") + on + "\n"), *quit) == std::optional<std::string>("1"));
    for (const char *off : {"0", "disabled", "false", "no", "off", "Enabled"})
        CHECK(option_value(Config::parse(std::string("close_game_on_quit = ") + off + "\n"), *quit) == std::optional<std::string>("0"));
    // upnp: Sunshine's key, off by default and read the same way, so a sunshine.conf copied from a PC shows as on.
    const OptionSpec *upnp = find_option("upnp");
    CHECK(upnp != nullptr);
    CHECK(upnp->kind == OptionKind::toggle);
    CHECK(std::string_view(upnp->default_label) == "Off");
    CHECK(!option_value(Config::parse("port = 48000\n"), *upnp));
    CHECK(option_value(Config::parse("upnp = enabled\n"), *upnp) == std::optional<std::string>("1"));
    CHECK(option_value(Config::parse("upnp = disabled\n"), *upnp) == std::optional<std::string>("0"));
    // pair_over_internet: the host's own key, which Sunshine's config reads (patch 0030); off by default, read the same way.
    const OptionSpec *pair = find_option("pair_over_internet");
    REQUIRE(pair != nullptr);
    CHECK(pair->kind == OptionKind::toggle);
    CHECK(std::string_view(pair->default_label) == "Off");
    CHECK(!validate(*pair, "1"));
    CHECK(!option_value(Config::parse("port = 48000\n"), *pair));
    CHECK(option_value(Config::parse("pair_over_internet = enabled\n"), *pair) == std::optional<std::string>("1"));
    CHECK(option_value(Config::parse("pair_over_internet = 0\n"), *pair) == std::optional<std::string>("0"));
    // fec_percentage: Sunshine's own key, a number up to 80 (above it Sunshine stops making room for it in the bitrate).
    const OptionSpec *fec = find_option("fec_percentage");
    REQUIRE(fec != nullptr);
    CHECK(fec->kind == OptionKind::number);
    CHECK(fec->fallback == 20 && std::string_view(fec->default_label) == "20 %");
    CHECK(!validate(*fec, "0"));
    CHECK(!validate(*fec, "20"));
    CHECK(!validate(*fec, "80"));
    CHECK(validate(*fec, "81"));
    CHECK(validate(*fec, "-1"));
    CHECK(option_value(Config::parse("fec_percentage = 35\n"), *fec) == std::optional<std::string>("35"));
    CHECK(std::string_view(fec->unit) == "%");
    CHECK(std::string_view(find_option("ping_timeout")->unit) == "ms");
    // Other options stay as they are in the file.
    CHECK(option_value(Config::parse("experimental_hevc = on\n"), *find_option("experimental_hevc")) == std::optional<std::string>("on"));
}

static void autoload_tests()
{
    // The list on the test console (2026-10-29), without a trailing newline after the last entry.
    const std::string list = "repo/kstuff-lite@e8cce5d1\n!1000\nrepo/elfldr@e8cce5d1\n!1000\nrepo/nanodns@e8cce5d1";
    CHECK(!autoload::contains(list, autoload::kSlotKey));
    const std::string on = autoload::with_entry(list, autoload::kSlotKey);
    CHECK(on == list + "\nlocal/prospero-sunshine\n");
    CHECK(autoload::contains(on, autoload::kSlotKey));
    CHECK(autoload::with_entry(on, autoload::kSlotKey) == on);
    const std::string off = autoload::without_entry(on, autoload::kSlotKey);
    CHECK(off == list + "\n");
    CHECK(!autoload::contains(off, autoload::kSlotKey));
    CHECK(autoload::with_entry("", autoload::kSlotKey) == "local/prospero-sunshine\n");
    // Every line that names the slot goes, including one with stray spaces.
    CHECK(autoload::without_entry(" local/prospero-sunshine \n!500\nlocal/prospero-sunshine\n", autoload::kSlotKey) ==
          "!500\n");
    // Another slot whose name starts the same is left alone.
    CHECK(autoload::contains("local/prospero-sunshine-2\n", autoload::kSlotKey) == false);

    CHECK(autoload::manager_autoload_enabled("AUTOLOAD_ENABLED=1\nAUTOLOAD_DELAY=5\n"));
    CHECK(!autoload::manager_autoload_enabled("AUTOLOAD_ENABLED=0\n"));
    CHECK(!autoload::manager_autoload_enabled("AUTOLOAD_DELAY=5\n"));

    const std::string meta = autoload::slot_meta("prospero_sunshine.elf", "2026-10-29T00:00:00+0000");
    CHECK(meta.find("\"kind\": \"local\"") != std::string::npos);
    CHECK(meta.find("\"filename\": \"prospero_sunshine.elf\"") != std::string::npos);
}

// The build's commit, and how the wait for the resource ended.
static void capture_line_tests()
{
    diag::HostLog log = diag::parse_host_log("ProsperoSunshine: host version=0.1.7 commit=3b912d6a firmware=0x10400000 known=0\n");
    CHECK(log.version == "0.1.7");
    CHECK(log.commit == "3b912d6a");
    CHECK(log.firmware == 0x10400000);
    CHECK(diag::parse_host_log("host version=0.1.7 commit=unknown firmware=0x10400000 known=0\n").commit == "unknown");
    CHECK(diag::parse_host_log("host version=0.1.6 firmware=0x10400000 known=0\n").commit.empty()); // an older host

    const std::string open = "capture open start codec=0 resource=180\ncapture RUNNING rc=0\n";
    const std::string end = "capture open failed step=RUNNING error=-1 rc=0\n";
    log = diag::parse_host_log(open + "capture RUNNING outcome=refused value=3 waited_ms=120 list_rc=0 active=175,177\n" + end);
    CHECK(log.running.outcome == "refused");
    CHECK(log.running.value == "3");
    CHECK(log.running.waited_ms == "120");
    CHECK(log.running.active == "175,177");
    CHECK(diag::capture_summary(log) == "last open failed at RUNNING (error -1, rc 0): the system refused the resource, data[0]=3, "
                                        "waited 120 ms, active resources: 175,177");
    log = diag::parse_host_log(open + "capture RUNNING outcome=timeout value=-1 waited_ms=2000 list_rc=0 active=none\n" + end);
    CHECK(diag::capture_summary(log) == "last open failed at RUNNING (error -1, rc 0): no answer from the system, "
                                        "waited 2000 ms, active resources: none");
    log = diag::parse_host_log(open + "capture RUNNING outcome=revoked value=-1 waited_ms=40 list_rc=0x80000001 active=none\n" + end);
    CHECK(diag::capture_summary(log) == "last open failed at RUNNING (error -1, rc 0): the system took the resource away, "
                                        "waited 40 ms, active resources: none (list rc 0x80000001)");
    // A later attempt has its own story.
    log = diag::parse_host_log(open + "capture RUNNING outcome=timeout value=-1 waited_ms=2000 list_rc=0 active=none\n" + end +
                               "capture open start codec=0 resource=180\ncapture open ok\n");
    CHECK(log.running.outcome.empty());
    CHECK(diag::capture_summary(log) == "last open ok");

    // Other steps read as before.
    log = diag::parse_host_log("capture open start codec=0\ncapture open failed step=OpenVideo error=-1 rc=0x80c78000\n");
    CHECK(diag::capture_summary(log) == "last open failed at OpenVideo (error -1, rc 0x80c78000)");
    CHECK(diag::capture_summary(diag::parse_host_log("")) == "not tried since the host started");
    log = diag::parse_host_log("capture open start codec=0\ncapture register rc=0\n");
    CHECK(diag::capture_summary(log) == "last open has no outcome, after: register rc=0");

    // Each line starts with a time stamp; the debug build adds "->" lines and the self-check's open.
    log = diag::parse_host_log("ProsperoSunshine: host version=0.3.4-debug commit=0a1b2c3d firmware=0x10200006 known=1 "
                               "started=2026-11-06T14:00:00\n"
                               "[     12] ProsperoSunshine: selfcheck firmware=0x10200006 known=1 console=PS5-Pro\n"
                               "[    900] ProsperoSunshine: capture open start codec=0 resource=180\n"
                               "[    950] ProsperoSunshine: -> capture OpenVideo codec=0 mode=0x13c\n"
                               "[    990] ProsperoSunshine: capture OpenVideo rc=0\n"
                               "[    991] ProsperoSunshine: capture open failed step=open only error=-1 rc=0\n");
    CHECK(log.version == "0.3.4-debug" && log.commit == "0a1b2c3d" && log.firmware == 0x10200006u);
    CHECK(diag::capture_summary(log) == "not tried since the host started");
    log = diag::parse_host_log("[ 123456] ProsperoSunshine: capture open start codec=1 resource=180\n"
                               "[ 123500] ProsperoSunshine: capture video out mode 0x13c\n"
                               "[ 123501] ProsperoSunshine: -> capture OpenVideo codec=1 mode=0x13c\n");
    CHECK(diag::capture_summary(log) == "last open has no outcome, after: OpenVideo codec=1 mode=0x13c (called, no answer)");
    log = diag::parse_host_log("[      5] ProsperoSunshine: capture open start codec=0\n"
                               "[      9] ProsperoSunshine: capture open failed step=OpenVideo error=-1 rc=0x81950008\n"
                               "[     10] ProsperoSunshine: host stopped\n");
    CHECK(diag::capture_summary(log) == "last open failed at OpenVideo (error -1, rc 0x81950008)");
    CHECK(log.stopped);
    // A line that only looks like a time stamp stays as it is.
    CHECK(diag::parse_host_log("[x] host stopped\n").stopped == false);

    // The lock no switch can lift stays visible.
    log = diag::parse_host_log("ra refusing firmware 0x9000000: ResourceArbitrator calls not resolved\n");
    CHECK(diag::capture_summary(log).find("refused: the ResourceArbitrator calls were not found") == 0);
    CHECK(diag::capture_summary(log).find("no switch can help") != std::string::npos);
}

// The lines as src/platform/ps5/capture.c writes them; tests/capture_open_test.c checks that side.
static void experimental_tests()
{
    // Where a switch counts.
    CHECK(!diag::experimental_needed(0x13000040, "experimental_hevc"));
    CHECK(!diag::experimental_needed(0x13000040, "experimental_hdr"));
    CHECK(!diag::experimental_needed(0x13000040, "experimental_large_modes"));
    CHECK(!diag::experimental_needed(0x10200006, "experimental_hevc"));
    CHECK(!diag::experimental_needed(0x10200006, "experimental_hdr"));
    CHECK(diag::experimental_needed(0x10200006, "experimental_large_modes"));
    CHECK(diag::experimental_needed(0x13600000, "experimental_hevc"));
    CHECK(diag::experimental_needed(0x13600000, "experimental_hdr"));
    CHECK(diag::experimental_needed(0x13600000, "experimental_large_modes"));
    CHECK(diag::experimental_needed(0x10400000, "experimental_hevc"));
    CHECK(diag::experimental_needed(0, "experimental_hevc"));
    // The bitrate: verified on 13.00, and no rate fields to act on with 10.x.
    CHECK(!diag::experimental_needed(0x13000040, "experimental_high_bitrate"));
    CHECK(!diag::experimental_no_effect(0x13000040, "experimental_high_bitrate"));
    CHECK(!diag::experimental_needed(0x10200006, "experimental_high_bitrate"));
    CHECK(diag::experimental_no_effect(0x10200006, "experimental_high_bitrate"));
    CHECK(!diag::experimental_needed(0x10400000, "experimental_high_bitrate"));
    CHECK(diag::experimental_no_effect(0x10400000, "experimental_high_bitrate"));
    CHECK(!diag::experimental_needed(0x09600000, "experimental_high_bitrate"));
    CHECK(diag::experimental_no_effect(0x09600000, "experimental_high_bitrate"));
    CHECK(!diag::experimental_no_effect(0x10400000, "experimental_large_modes"));
    CHECK(diag::experimental_needed(0x13600000, "experimental_high_bitrate"));
    CHECK(diag::experimental_needed(0x12400000, "experimental_high_bitrate"));
    CHECK(diag::experimental_needed(0, "experimental_high_bitrate"));
    // Modes below 1080p: verified on 10.20 and 13.00, a switch elsewhere.
    CHECK(!diag::experimental_needed(0x10200006, "experimental_small_modes"));
    CHECK(!diag::experimental_no_effect(0x10200006, "experimental_small_modes"));
    CHECK(!diag::experimental_needed(0x13000040, "experimental_small_modes"));
    CHECK(!diag::experimental_no_effect(0x13000040, "experimental_small_modes"));
    CHECK(diag::experimental_needed(0x10400000, "experimental_small_modes"));
    CHECK(!diag::experimental_no_effect(0x10400000, "experimental_small_modes"));
    CHECK(diag::experimental_needed(0x13600000, "experimental_small_modes"));

    // The options: switches, off by default, written as 1.
    for (const char *key : {"experimental_hevc", "experimental_hdr", "experimental_large_modes", "experimental_high_bitrate",
                            "experimental_small_modes"})
    {
        const OptionSpec *spec = find_option(key);
        REQUIRE(spec != nullptr);
        CHECK(spec->kind == OptionKind::toggle);
        CHECK(!validate(*spec, "1"));
        CHECK(!validate(*spec, "0"));
        CHECK(validate(*spec, "yes"));
    }
    Config c = Config::parse("port = 48000\n");
    c.set("experimental_hevc", "1");
    CHECK(c.text() == "port = 48000\nexperimental_hevc = 1\n");
    c.set("experimental_hevc", std::nullopt);
    CHECK(c.text() == "port = 48000\n");

    // The first variant fails at Start, the second runs.
    const std::string header = "ProsperoSunshine: host version=0.1.7 firmware=0x13600000 known=0\n"
                               "ProsperoSunshine: experimental switches=hevc set=hevc firmware=0x13600000\n";
    const std::string fields = " switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 variant=";
    const std::string ran = header + "ProsperoSunshine: experimental attempt" + fields + "minimal\n" +
                            "ProsperoSunshine: capture open start codec=1 resource=180\n"
                            "ProsperoSunshine: capture Start rc=0x80c78000\n"
                            "ProsperoSunshine: capture open failed step=Start error=-1 rc=0\n"
                            "ProsperoSunshine: experimental result=failed" + fields + "minimal error=-1 rc=0x80c78000 step=Start\n" +
                            "ProsperoSunshine: experimental attempt" + fields + "full\n" +
                            "ProsperoSunshine: capture open start codec=1 resource=180\n"
                            "ProsperoSunshine: capture open ok\n"
                            "ProsperoSunshine: experimental result=ok" + fields + "full error=0 rc=0 step=none\n";
    diag::HostLog log = diag::parse_host_log(ran);
    CHECK(log.switches == "hevc");
    CHECK(log.switches_set == "hevc");
    CHECK(log.attempts.size() == 2);
    CHECK(!log.attempts[0].ok);
    CHECK(log.attempts[0].variant == "minimal");
    CHECK(log.attempts[0].step == "Start");
    CHECK(log.attempts[0].rc == "0x80c78000");
    CHECK(log.attempts[0].codec == "hevc");
    CHECK(log.attempts[0].bits == "8");
    CHECK(log.attempts[0].mode == "1920x1080");
    CHECK(log.attempts[0].fps == "60");
    CHECK(log.attempts[1].ok);
    CHECK(log.attempts[1].variant == "full");
    CHECK(log.disabled.empty());
    CHECK(log.pending_trial.empty());
    CHECK(log.capture == diag::Capture::ok);
    diag::Notice note = diag::notice(0x13600000, log, true);
    CHECK(!note.bad);
    CHECK(note.text == "Experimental HEVC started on Firmware 13.60. Is the picture right? Please post the Diagnostics report on GitHub.");
    std::string report = diag::experimental_report(log, {"experimental_hevc"});
    CHECK(report.find("In sunshine.conf: experimental_hevc\n") != std::string::npos);
    CHECK(report.find("started with: hevc, and uses: hevc\n") != std::string::npos);
    CHECK(report.find("hevc 8 bit 1920x1080@60 20000 kbit/s variant minimal trial hevc: failed at Start, rc 0x80c78000\n") != std::string::npos);
    CHECK(report.find("hevc 8 bit 1920x1080@60 20000 kbit/s variant full trial hevc: ok, rc 0\n") != std::string::npos);

    // Everything fails: the failure stays on the page, also after H.264 ran. A step's name may hold spaces.
    const std::string lost = header + "ProsperoSunshine: experimental result=failed" + fields + "minimal error=-1 rc=0 step=first frame\n" +
                             "ProsperoSunshine: experimental result=failed" + fields + "full error=-1 rc=0x80c78000 step=encoder memory\n" +
                             "ProsperoSunshine: experimental disabled feature=hevc\n"
                             "ProsperoSunshine: capture open failed step=OpenVideo error=-1 rc=0\n"
                             "ProsperoSunshine: capture open start codec=0 resource=180\n"
                             "ProsperoSunshine: capture open ok\n";
    log = diag::parse_host_log(lost);
    CHECK(log.attempts[0].step == "first frame");
    CHECK(log.attempts[1].step == "encoder memory");
    CHECK(log.disabled.size() == 1);
    note = diag::notice(0x13600000, log, true);
    CHECK(note.bad);
    CHECK(note.text == "Experimental HEVC failed on Firmware 13.60 (off until the host restarts). Please post the Diagnostics report on GitHub.");
    report = diag::experimental_report(log, {});
    CHECK(report.find("In sunshine.conf: none\n") != std::string::npos);
    CHECK(report.find("failed at first frame, rc 0\n") != std::string::npos);
    CHECK(report.find("Off until the host restarts: HEVC\n") != std::string::npos);

    // Several functions, named once each.
    log = diag::parse_host_log("experimental disabled feature=hevc\nexperimental disabled feature=large\nexperimental disabled feature=hevc\n");
    CHECK(diag::notice(0x13600000, log, true).text.find("Experimental HEVC and 1440p/4K failed on Firmware 13.60") == 0);

    // A high bitrate fails: the rate is in the file and the report, and the stream ran on at 50.
    const std::string bitrate = "ProsperoSunshine: host version=0.1.7 firmware=0x13600000 known=0\n"
                                "ProsperoSunshine: experimental switches=bitrate set=bitrate firmware=0x13600000\n"
                                "ProsperoSunshine: experimental result=failed switches=bitrate firmware=0x13600000 trial=bitrate codec=h264 bits=8 "
                                "mode=1920x1080 fps=60 kbit=100000 variant=standard error=-1 rc=0x80c78000 step=Start\n"
                                "ProsperoSunshine: experimental result=ok switches=bitrate firmware=0x13600000 trial=none codec=h264 bits=8 "
                                "mode=1920x1080 fps=60 kbit=50000 variant=standard error=0 rc=0 step=none\n"
                                "ProsperoSunshine: experimental disabled feature=bitrate\n";
    log = diag::parse_host_log(bitrate);
    CHECK(log.attempts.size() == 2);
    CHECK(log.attempts[0].kbit == "100000");
    CHECK(log.attempts[0].step == "Start");
    CHECK(log.attempts[1].kbit == "50000");
    CHECK(diag::notice(0x13600000, log, true).text ==
          "Experimental high bitrate failed on Firmware 13.60 (off until the host restarts). Please post the Diagnostics report on GitHub.");
    report = diag::experimental_report(log, {"experimental_high_bitrate"});
    CHECK(report.find("h264 8 bit 1920x1080@60 100000 kbit/s variant standard trial bitrate: failed at Start, rc 0x80c78000\n") != std::string::npos);
    CHECK(report.find("h264 8 bit 1920x1080@60 50000 kbit/s variant standard trial none: ok, rc 0\n") != std::string::npos);
    CHECK(report.find("Off until the host restarts: high bitrate\n") != std::string::npos);
    // And when it ran.
    log = diag::parse_host_log("experimental result=ok switches=bitrate firmware=0x13600000 trial=bitrate codec=h264 bits=8 "
                               "mode=1920x1080 fps=60 kbit=100000 variant=standard error=0 rc=0 step=none\n");
    CHECK(diag::notice(0x13600000, log, true).text.find("Experimental high bitrate started on Firmware 13.60") == 0);

    // The host went down inside an attempt: no outcome, no host. While it runs the attempt is simply under way.
    log = diag::parse_host_log(header + "ProsperoSunshine: experimental attempt" + fields + "minimal\n" +
                               "ProsperoSunshine: capture open start codec=1 resource=180\n");
    CHECK(log.pending_trial == "hevc");
    CHECK(diag::notice(0x13600000, log, false).bad);
    CHECK(diag::notice(0x13600000, log, false).text ==
          "Experimental HEVC took the host down on Firmware 13.60. Please post the Diagnostics report on GitHub.");
    CHECK(!diag::notice(0x13600000, log, true).bad);
    // The next host says so itself.
    log = diag::parse_host_log(header + "ProsperoSunshine: experimental result=failed" + fields + "minimal error=0 rc=0 step=host went down\n" +
                               "ProsperoSunshine: experimental disabled feature=hevc\n");
    CHECK(log.attempts[0].step == "host went down");
    CHECK(diag::notice(0x13600000, log, true).bad);

    // A plain H.264 stream with a switch on proves nothing: only the firmware's caution.
    log = diag::parse_host_log(header + "ProsperoSunshine: experimental attempt switches=hevc firmware=0x13600000 trial=none codec=h264 bits=8 "
                                        "mode=1920x1080 fps=60 kbit=20000 variant=standard\n" +
                               "ProsperoSunshine: experimental result=ok switches=hevc firmware=0x13600000 trial=none codec=h264 bits=8 "
                               "mode=1920x1080 fps=60 kbit=20000 variant=standard error=0 rc=0 step=none\n");
    CHECK(log.pending_trial.empty());
    CHECK(diag::notice(0x13600000, log, true).text.find("is untested") != std::string::npos);

    // Switches ignored on verified firmware: nothing to tell on the home page; the report says so.
    log = diag::parse_host_log("host version=0.1.7 firmware=0x13000040 known=1\nexperimental switches=none set=hevc,hdr,large firmware=0x13000040\n");
    CHECK(log.switches == "none");
    CHECK(log.switches_set == "hevc,hdr,large");
    CHECK(diag::notice(0x13000040, log, true).text.empty());
    CHECK(diag::experimental_report(log, {}).find("started with: hevc,hdr,large, and uses: none\n") != std::string::npos);
    CHECK(diag::experimental_report(log, {}).find("No attempt since the host started.\n") != std::string::npos);
    // Without a switch the report's part stays short and the file has no such line.
    log = diag::parse_host_log("host version=0.1.7 firmware=0x13000040 known=1\ncapture open start codec=0 resource=180\ncapture open ok\n");
    CHECK(log.attempts.empty());
    CHECK(diag::experimental_report(log, {}).find("started with: none, and uses: none\n") != std::string::npos);

    // Only the parsed fields reach the report's part, so other text in such a line stays out.
    log = diag::parse_host_log("experimental attempt switches=hevc client=192.168.1.20 name=Living\n"
                               "experimental result=ok switches=hevc client=192.168.1.20 name=Living trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 "
                               "variant=full error=0 rc=0 step=none\n");
    report = diag::experimental_report(log, {});
    CHECK(report.find("192.168") == std::string::npos);
    CHECK(report.find("Living") == std::string::npos);

    // Each function in use has its result and rc in the report, success or failure.
    log = diag::parse_host_log(ran);
    report = diag::experimental_report(log, {});
    CHECK(report.find("  HEVC: ok, rc 0 (full)\n") != std::string::npos);
    log = diag::parse_host_log(lost);
    report = diag::experimental_report(log, {});
    CHECK(report.find("  HEVC: failed at encoder memory, rc 0x80c78000 (full), off until the host restarts\n") != std::string::npos);
    log = diag::parse_host_log(bitrate);
    report = diag::experimental_report(log, {});
    CHECK(report.find("  high bitrate: failed at Start, rc 0x80c78000 (standard), off until the host restarts\n") != std::string::npos);
    log = diag::parse_host_log("experimental switches=hevc,large set=hevc,large firmware=0x13600000\n");
    report = diag::experimental_report(log, {});
    CHECK(report.find("  HEVC: not tried since the host started\n") != std::string::npos);
    CHECK(report.find("  1440p/4K: not tried since the host started\n") != std::string::npos);
    CHECK(report.find("  HDR:") == std::string::npos);

    // Every switch in the list is an option of the app, off by default.
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
    {
        const OptionSpec *spec = find_option(ps_exp_switch_at(i)->key);
        REQUIRE(spec != nullptr);
        CHECK(spec->kind == OptionKind::toggle);
        CHECK(std::string(spec->label) == ps_exp_switch_at(i)->label);
    }

    // The list keeps the latest attempts.
    std::string many;
    for (std::size_t i = 0; i < diag::kMaxAttempts + 5; ++i)
        many += "experimental result=failed trial=hevc codec=hevc bits=8 mode=1920x1080 fps=" + std::to_string(i) + " variant=full rc=0 step=Start\n";
    log = diag::parse_host_log(many);
    CHECK(log.attempts.size() == diag::kMaxAttempts);
    CHECK(log.attempts.back().fps == std::to_string(diag::kMaxAttempts + 4));
}

// The host's STREAMS lines (src/control/stream_list.hpp writes them).
static void stream_tests()
{
    diag::Stream s;
    CHECK(diag::parse_stream_line("OK uuid=4D7B-22 address=192.168.1.20 codec=HEVC hdr=1 mode=1920x1080@60 kbps=20000 secs=754 "
                                  "pads=1 role=capture name=Living room PC",
                                  &s));
    CHECK(s.id == "4D7B-22" && s.address == "192.168.1.20" && s.codec == "HEVC" && s.hdr && s.mode == "1920x1080@60");
    CHECK(s.kbps == 20000 && s.seconds == 754 && s.pads == 1 && s.role == "capture" && s.name == "Living room PC");
    CHECK(diag::stream_label(s) == "Living room PC");
    CHECK(diag::stream_info(s) == "HEVC HDR, 1920x1080@60, 20 Mbit/s, 1 controller");

    // "-" for what the host does not know; an empty name; a name that holds "name=" itself.
    CHECK(diag::parse_stream_line("OK uuid=- address=- codec=H.264 hdr=0 mode=- kbps=0 secs=0 pads=0 role=starting name=", &s));
    CHECK(s.id.empty() && s.address.empty() && s.mode.empty() && !s.hdr && s.kbps == 0 && s.name.empty());
    CHECK(diag::stream_label(s) == "Unnamed device");
    CHECK(diag::stream_info(s) == "H.264, no controller");
    CHECK(diag::parse_stream_line("OK uuid=A codec=H.264 kbps=6500 pads=3 role=joined name=Deck name=2", &s));
    CHECK(s.name == "Deck name=2" && s.kbps == 6500 && s.pads == 3 && s.role == "joined" && s.address.empty());
    CHECK(diag::stream_info(s) == "H.264, 6.5 Mbit/s, 3 controllers");

    // Keys in another order and keys a newer host adds; values that are not numbers count as 0.
    CHECK(diag::parse_stream_line("OK role=joined future=x uuid=B kbps=abc secs=-5 pads=1 name=Phone", &s));
    CHECK(s.id == "B" && s.role == "joined" && s.kbps == 0 && s.seconds == 0 && s.name == "Phone" && s.codec.empty());
    CHECK(diag::stream_info(s) == "Video, 1 controller");
    s.kbps = 800;
    CHECK(diag::stream_info(s) == "Video, 800 kbit/s, 1 controller");

    // What reaches the device (0.3): unknown from an older host or "-", then rate, round trip and losses.
    CHECK(s.sent_kbps == -1 && s.rtt_ms == -1 && s.recoveries == 0 && diag::stream_network(s).empty());
    CHECK(diag::parse_stream_line("OK uuid=C codec=HEVC kbps=20000 pads=1 role=capture channel=0 sent_kbps=18320 rtt_ms=4 "
                                  "recoveries=2 name=PC",
                                  &s));
    CHECK(s.sent_kbps == 18320 && s.rtt_ms == 4 && s.recoveries == 2);
    CHECK(diag::stream_network(s) == "18 Mbit/s \xC2\xB7 4 ms \xC2\xB7 2 losses");
    CHECK(diag::parse_stream_line("OK uuid=C sent_kbps=- rtt_ms=0 recoveries=1 name=PC", &s));
    CHECK(s.sent_kbps == -1 && s.rtt_ms == 0 && diag::stream_network(s) == "0 ms \xC2\xB7 1 loss");
    s.sent_kbps = 6540;
    s.recoveries = 0;
    CHECK(diag::stream_network(s) == "6.5 Mbit/s \xC2\xB7 0 ms");

    // Not stream lines.
    CHECK(!diag::parse_stream_line("END", &s));
    CHECK(!diag::parse_stream_line("ERR unknown command", &s));
    CHECK(!diag::parse_stream_line("OK uuid=A address=1.2.3.4", &s)); // no name
    CHECK(!diag::parse_stream_line("OK address=1.2.3.4 name=A", &s)); // no uuid
    CHECK(!diag::parse_stream_line("OK 4D7B-22 0 1280x720@60 PS Vita", &s)); // a DEVICES line

    CHECK(diag::duration_text(0) == "0 s");
    CHECK(diag::duration_text(45) == "45 s");
    CHECK(diag::duration_text(60) == "1 min");
    CHECK(diag::duration_text(754) == "12 min");
    CHECK(diag::duration_text(3600 + 5 * 60 + 7) == "1 h 05 min");
    CHECK(diag::duration_text(26 * 3600 + 40 * 60) == "26 h 40 min");
    CHECK(diag::duration_text(-1) == "0 s");
}

// What the report hides and how (app/src/psapp/mask.hpp, diag.hpp "Scrubber"): tags that say what was hidden, stay the same for the same
// value within one report, and match between a device's name and its uuid. The test of every kind of log line is
// tests/privacy_leak_test.cpp.
static void scrubber_tests()
{
    {
        diag::Scrubber scrubber;
        scrubber.add_console("Deck Room");
        scrubber.add_device("Tom's iPhone", "3FA2c1d0-1111-2222-3333-444455556666");
        scrubber.add_device("Tom", "9b00aaaa-1111-2222-3333-444455556666");
        scrubber.add_name("Guest Pad");
        scrubber.add_name("Guest Pad");
        scrubber.add_name("PC");
        CHECK(scrubber.apply("by Tom's iPhone and tom and DECK ROOM") == "by <device-3fa2> and <device-9b00> and <console-name>");
        CHECK(scrubber.apply("a Guest Pad b Guest Pad") == "a <name-1> b <name-1>");
        CHECK(scrubber.apply("PC stays") == "PC stays");
        scrubber.add_console("PS5");
        CHECK(scrubber.apply("PS5 capture requested") == "PS5 capture requested");
        // The uuid of a device has the first characters of its name's tag, the other ids are numbered.
        CHECK(scrubber.apply("uuid 3fa2c1d0-1111-2222-3333-444455556666 uniqueid ABCDEF0123456789 again ABCDEF0123456789") ==
              "uuid <uuid-3fa2> uniqueid <id-1> again <id-1>");
        // One address, one tag, in every call; the kind is in the tag.
        CHECK(scrubber.apply("a 192.168.1.9 b 203.0.113.5") == "a <lan-1> b <public-1>");
        CHECK(scrubber.apply("c 203.0.113.5 d 192.168.1.9 e 2001:db8::9 f 100.64.0.2") == "c <public-1> d <lan-1> e <public6-1> f <tailscale-1>");
        CHECK(scrubber.apply("mac 00:11:22:33:44:55") == "mac <mac-1>");
        CHECK(scrubber.apply("rtsp://ps5.home.example.org:48010") == "rtsp://<host-1>:48010");
        CHECK(scrubber.apply("127.0.0.1 and 0.0.0.0 stay") == "127.0.0.1 and 0.0.0.0 stay");
        // Without names (the last pass over the whole report) only what has a shape goes.
        CHECK(scrubber.apply("Tom 192.168.1.9 PPSA99310", false) == "Tom <lan-1> <title>");
    }
    {
        // Tags a host in the debug build made are not given out again for something else.
        diag::Scrubber scrubber;
        CHECK(scrubber.apply("<lan-4> was 192.168.1.9") == "<lan-4> was <lan-5>");
    }
    {
        // A cut in the middle of an address would leave half of it: the scrub comes first, then the cut.
        diag::Scrubber scrubber;
        const std::string line = "Info: client from 192.168.1.200 asked for something that makes this line longer than the cut";
        const std::string cut = scrubber.tail(line, 5, 30);
        CHECK(cut.find("192") == std::string::npos && cut.find("168") == std::string::npos);
        CHECK(cut == "Info: client from <lan-1> aske\n");
        CHECK(scrubber.tail("a\nb\nc\nd\n", 2, 10) == "c\nd\n");
        // Long lines are cut after the scrub, not before: what the scrub made of an address is not cut in two by the width of 4096
        // it works with.
        CHECK(scrubber.tail(std::string(4000, 'x') + " 10.1.2.3\n", 1, 5000).find("<lan-2>") != std::string::npos);
    }
    {
        // A name that is part of another is hidden whole, the longer first.
        diag::Scrubber scrubber;
        scrubber.add_name("Tom");
        scrubber.add_name("Tom's iPhone");
        CHECK(scrubber.apply("Tom's iPhone and Tom") == "<name-2> and <name-1>");
        // Lines of dashes and short hex stay.
        CHECK(scrubber.apply("----------------------------------------") == "----------------------------------------");
        CHECK(scrubber.apply("commit=8fa42cd5 rc=0xe034") == "commit=8fa42cd5 rc=0xe034");
    }
}

static void diag_tests()
{
    CHECK(diag::firmware_text(0x13000040) == "13.00");
    CHECK(diag::firmware_text(0x10400000) == "10.40");
    CHECK(diag::firmware_verified(0x13000040));
    CHECK(diag::firmware_verified(0x10200006));
    CHECK(!diag::firmware_verified(0x10400000));
    CHECK(!diag::firmware_verified(0x13600000));

    // A good run.
    const std::string good = "ProsperoSunshine: host version=0.1.3 firmware=0x13000040 known=1\n"
                             "ProsperoSunshine: host ready control_port=47995\n"
                             "ProsperoSunshine: capture open start codec=1\n"
                             "ProsperoSunshine: capture OpenVideo rc=0\n"
                             "ProsperoSunshine: capture open ok\n";
    diag::HostLog log = diag::parse_host_log(good);
    CHECK(log.version == "0.1.3");
    CHECK(log.firmware == 0x13000040);
    CHECK(log.capture == diag::Capture::ok);
    CHECK(log.last_step.empty());
    CHECK(!log.stopped);
    CHECK(diag::notice(log.firmware, log, true).text.empty());
    CHECK(diag::parse_host_log(good + "ProsperoSunshine: host stopped\n").stopped);

    // No file yet: only the firmware can speak.
    CHECK(diag::parse_host_log("").capture == diag::Capture::unknown);
    CHECK(diag::notice(0x13000040, {}, false).text.empty());
    // The control socket's development switch goes in front of the home page's line, as a failure.
    CHECK(diag::with_control_lan({}, false).text.empty());
    CHECK(diag::with_control_lan({}, true).bad);
    CHECK(diag::with_control_lan({}, true).text.find("control_lan") != std::string::npos);
    CHECK(diag::with_control_lan({"Firmware 13.60 is untested.", false}, true).text.find(" Firmware 13.60 is untested.") !=
          std::string::npos);
    CHECK(diag::with_control_lan({"Firmware 13.60 is untested.", false}, false).text == "Firmware 13.60 is untested.");
    diag::Notice note = diag::notice(0x10400000, {}, false);
    CHECK(note.text.find("Firmware 10.40 is untested") == 0);
    CHECK(!note.bad);

    // A failed step, then a later attempt that works: the latest outcome counts.
    const std::string failed = "ProsperoSunshine: host version=0.1.3 firmware=0x10400000 known=0\n"
                               "ProsperoSunshine: capture open start codec=0\n"
                               "ProsperoSunshine: capture OpenVideo rc=0x80c78000\n"
                               "ProsperoSunshine: capture open failed step=OpenVideo error=-1 rc=0x80c78000\n";
    log = diag::parse_host_log(failed);
    CHECK(log.capture == diag::Capture::failed);
    CHECK(log.failed_step == "OpenVideo");
    CHECK(log.failed_error == -1);
    CHECK(log.failed_rc == "0x80c78000");
    note = diag::notice(log.firmware, log, true);
    CHECK(note.bad);
    CHECK(note.text == "Capture failed at OpenVideo on Firmware 10.40. See Diagnostics.");
    log = diag::parse_host_log(failed + "ProsperoSunshine: capture open start codec=0\nProsperoSunshine: capture open ok\n");
    CHECK(log.capture == diag::Capture::ok);
    CHECK(!diag::notice(log.firmware, log, true).bad);

    // Causes the user can act on, whatever the firmware.
    log = diag::parse_host_log("capture open start codec=0\ncapture open failed step=video error=-4 rc=0\n");
    CHECK(diag::notice(0x13000040, log, true).text.find("PS VR") != std::string::npos);
    log = diag::parse_host_log("capture open start codec=0\ncapture open failed step=resource list error=-2 rc=0\n");
    CHECK(log.failed_step == "resource list");
    CHECK(diag::notice(0x13000040, log, true).text.find("Something else") == 0);

    // The refusal of an unknown firmware.
    log = diag::parse_host_log("ProsperoSunshine: ra refusing firmware 0x9000000: ResourceArbitrator calls not resolved\n");
    CHECK(log.refused);
    note = diag::notice(0x09000000, log, true);
    CHECK(note.bad);
    CHECK(note.text.find("Firmware 9.00 is not supported yet") == 0);

    // The process went down inside an attempt: no outcome line, and no host left.
    log = diag::parse_host_log("capture open start codec=0\ncapture register rc=0\ncapture RUNNING rc=0\n");
    CHECK(log.capture == diag::Capture::opening);
    CHECK(log.last_step == "RUNNING rc=0");
    CHECK(diag::notice(0x13000040, log, true).text.empty()); // still going
    CHECK(diag::notice(0x13000040, log, false).text.find("went down") != std::string::npos);

    // Scrubbing keeps the codes and takes what identifies someone.
    CHECK(diag::scrub("client 192.168.1.20 and 172.16.0.1:47989", {}) == "client <lan-1> and <lan-2>:47989");
    CHECK(diag::scrub("OpenVideo rc=0x80c78000 version 1.2.3", {}) == "OpenVideo rc=0x80c78000 version 1.2.3");
    CHECK(diag::scrub("from Living Room PC", {"Living Room PC"}) == "from <name-1>");
    CHECK(diag::scrub("name PC stays", {"PC"}) == "name PC stays");
    CHECK(diag::scrub("id 0123456789abcdef0123 end", {}) == "id <id-1> end");
    CHECK(diag::scrub("uuid 1b4e28ba-2fa1-11d2-883f-0016d3cca427.", {}) == "uuid <uuid-1b4e>.");
    CHECK(diag::scrub("firmware=0x13000040 known=1", {}) == "firmware=0x13000040 known=1");
    CHECK(diag::scrub("host version=0.1.7 commit=3b912d6a firmware", {}) == "host version=0.1.7 commit=3b912d6a firmware");
    CHECK(diag::scrub("capture RUNNING outcome=timeout active=175,177", {}) == "capture RUNNING outcome=timeout active=175,177");
    scrubber_tests();
    // Moonlight's placeholder name, and the name offered instead.
    CHECK(diag::pairing_label("roth") == "Moonlight");
    CHECK(diag::pairing_label("") == "Moonlight");
    CHECK(diag::pairing_label("Living Room") == "Living Room");
    CHECK(diag::suggested_device_name("roth", {}) == "Moonlight 1");
    CHECK(diag::suggested_device_name("", {"roth", "Moonlight 1"}) == "Moonlight 2");
    CHECK(diag::suggested_device_name("roth", {"Moonlight 1", "Moonlight 3"}) == "Moonlight 2");
    CHECK(diag::suggested_device_name("Steam Deck", {"Steam Deck"}) == "Steam Deck");
    // Device names and the host's device list.
    CHECK(diag::clean_device_name("  PS Vita ") == "PS Vita");
    CHECK(diag::clean_device_name("a\"b\\c[d]") == "abc[d]");
    CHECK(diag::clean_device_name("   ").empty());

    // Denying a pending request.
    CHECK(diag::deny_request("0123abcd") == "CANCELPIN 0123abcd\n");
    CHECK(diag::parse_deny("OK cancelled") == diag::DenyOutcome::denied);
    CHECK(diag::parse_deny("ERR no such request") == diag::DenyOutcome::gone);
    CHECK(diag::parse_deny("ERR unknown command") == diag::DenyOutcome::unsupported);
    CHECK(diag::parse_deny("ERR usage: CANCELPIN <id>") == diag::DenyOutcome::failed);
    CHECK(diag::parse_deny("") == diag::DenyOutcome::failed);
    {
        diag::Device d;
        CHECK(diag::parse_device_line("OK 4D7B-22 0 1280x720@60 PS Vita", &d));
        CHECK(d.id == "4D7B-22" && !d.streaming && d.mode == "1280x720@60" && d.name == "PS Vita");
        CHECK(diag::device_label(d) == "PS Vita");
        CHECK(diag::device_info(d) == "Last asked for 1280x720@60");
        d.streaming = true;
        CHECK(diag::device_info(d) == "Streaming now, 1280x720@60");
        CHECK(diag::parse_device_line("OK 4D7B-22 1 - ", &d));
        CHECK(d.streaming && d.mode.empty() && d.name.empty());
        CHECK(diag::device_label(d) == "Unnamed device");
        CHECK(diag::device_info(d) == "Streaming now");
        diag::Device vita;
        vita.name = "PS Vita";
        const std::vector<std::string> file{"Old PC"};
        CHECK(diag::paired_names(false, {vita}, file) == file);
        CHECK((diag::paired_names(true, {vita, d}, file) == std::vector<std::string>{"PS Vita", "Unnamed device"}));
        CHECK(diag::paired_names(true, {}, file).empty());
        d.streaming = false;
        CHECK(diag::device_info(d) == "No stream recorded yet");
        CHECK(!diag::parse_device_line("END", &d));
        CHECK(!diag::parse_device_line("ERR unknown command", &d));
        CHECK(!diag::parse_device_line("OK 4D7B-22", &d));
        CHECK(!diag::parse_device_line("OK 4D7B-22 0", &d));
        CHECK(!diag::parse_device_line("OK 4D7B-22 0 -", &d));
    }
    // What someone plays stays out; the app, the system's titles and "PS5 Screen" stay.
    CHECK(diag::scrub("Started PPSA99310", {}) == "Started <title>");
    CHECK(diag::scrub("Not starting PPSA03527: CUSA12345 is in front", {}) == "Not starting <title>: <title> is in front");
    CHECK(diag::scrub("/mnt/sandbox/PPSA99310_000 left", {}) == "/mnt/sandbox/<title>_000 left");
    CHECK(diag::scrub("/data/homebrew/PPSA47989 and NPXS40000", {}) == "/data/homebrew/PPSA47989 and NPXS40000");
    CHECK(diag::scrub("XPPSA99310 PPSA993101 PPSA9931", {}) == "XPPSA99310 PPSA993101 PPSA9931");
    CHECK(diag::scrub("Info: Session app [Mario 64] marked running\nnext", {}) == "Info: Session app [<title>] marked running\nnext");
    CHECK(diag::scrub("Session app [Game [Deluxe]] stopped", {}) == "Session app [<title>] stopped");
    CHECK(diag::scrub("Session app [ PS5 Screen] stopped", {}) == "Session app [ PS5 Screen] stopped");

    experimental_tests();
    capture_line_tests();
    stream_tests();

    CHECK(diag::tail("a\nb\nc\n", 2) == "b\nc\n");
    CHECK(diag::tail("a\nb\nc", 5) == "a\nb\nc\n");
    CHECK(diag::tail("", 5).empty());
    CHECK(diag::tail("abcdef\n", 1, 3) == "abc\n");
    CHECK(diag::errno_name(13) == "errno 13 (permission denied)");
    CHECK(diag::errno_name(9999) == "errno 9999");
}

static void web_tests()
{
    // The host's rules (src/web/web_auth.hpp): a one-word user, a printable password of 8 to 128.
    CHECK(web::valid_user("admin"));
    CHECK(web::valid_user("a"));
    CHECK(web::valid_user("Ann.B_c-9"));
    CHECK(web::valid_user(std::string(32, 'x')));
    CHECK(!web::valid_user(std::string(33, 'x')));
    CHECK(!web::valid_user(""));
    CHECK(!web::valid_user("two words"));
    CHECK(!web::valid_user("a@b"));
    CHECK(!web::valid_user("\xC3\xA4rger"));
    CHECK(web::user_problem("admin").empty());
    CHECK(!web::user_problem("").empty());
    CHECK(!web::user_problem("a b").empty());
    CHECK(!web::user_problem(std::string(33, 'x')).empty());

    CHECK(web::valid_password("12345678"));
    CHECK(web::valid_password("with spaces and ~!@#"));
    CHECK(web::valid_password(" leading and trailing "));
    CHECK(web::valid_password(std::string(128, 'p')));
    CHECK(!web::valid_password(std::string(129, 'p')));
    CHECK(!web::valid_password("1234567"));
    CHECK(!web::valid_password("tab\there12"));
    CHECK(!web::valid_password("line\nbreak12"));
    CHECK(!web::valid_password("\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4"));
    CHECK(web::password_problem("12345678").empty());
    CHECK(!web::password_problem("short").empty());
    CHECK(!web::password_problem(std::string(129, 'p')).empty());
    CHECK(!web::password_problem("tab\there12").empty());
    // The reason never repeats the password.
    CHECK(web::password_problem("secret!").find("secret") == std::string::npos);

    // Base port plus one; the default without the key or with a broken value.
    CHECK(web::port(std::nullopt) == 47990);
    CHECK(web::port(std::string("47989")) == 47990);
    CHECK(web::port(std::string("48000")) == 48001);
    CHECK(web::port(std::string("")) == 47990);
    CHECK(web::port(std::string("12a")) == 47990);
    CHECK(web::port(std::string("70000")) == 47990);
    CHECK(web::address("192.0.2.7", 47990) == "https://192.0.2.7:47990");
    CHECK(web::address("", 47990).empty());

    // The control lines.
    CHECK(web::set_request("admin", "pass word ") == "WEBLOGIN SET admin pass word \n");
    CHECK(web::set_request("u", " lead1234") == "WEBLOGIN SET u  lead1234\n");
    CHECK(std::string(web::kQuery) == "WEBLOGIN\n");
    CHECK(std::string(web::kOff) == "WEBLOGIN OFF\n");
    CHECK(std::string(web::kOn) == "WEBLOGIN ON\n");
    CHECK(std::string(web::kForget) == "WEBLOGIN FORGET\n");

    // The state: a host with the switch says enabled=, also for a login kept while the page is off.
    const auto on = web::parse_state("OK user=admin enabled=1");
    CHECK(on && on->user == "admin" && on->enabled && on->switchable);
    const auto kept = web::parse_state("OK user=admin enabled=0");
    CHECK(kept && kept->user == "admin" && !kept->enabled && kept->switchable);
    const auto none = web::parse_state("OK user= enabled=0");
    CHECK(none && none->user.empty() && !none->enabled && none->switchable);
    const auto odd_none = web::parse_state("OK user= enabled=1"); // nothing to be on with
    CHECK(odd_none && odd_none->user.empty() && !odd_none->enabled);
    const auto later = web::parse_state("OK user=admin enabled=1 more=x"); // fields added later are skipped
    CHECK(later && later->user == "admin" && later->enabled);
    // A host from before the switch: a name means on, and OFF removes the login.
    const auto old_on = web::parse_state("OK user=admin");
    CHECK(old_on && old_on->user == "admin" && old_on->enabled && !old_on->switchable);
    const auto old_off = web::parse_state("OK user=");
    CHECK(old_off && old_off->user.empty() && !old_off->enabled && !old_off->switchable);
    CHECK(!web::parse_state("ERR unknown command"));
    CHECK(!web::parse_state("OK"));
    CHECK(!web::parse_state(""));

    const web::Outcome set = web::parse_outcome("OK set");
    CHECK(set.ok && !set.unknown && set.reason.empty());
    CHECK(web::parse_outcome("OK off").ok);
    CHECK(web::parse_outcome("OK on").ok);
    CHECK(web::parse_outcome("OK forgotten").ok);
    const web::Outcome no_login = web::parse_outcome("ERR no login");
    CHECK(!no_login.ok && !no_login.unknown && no_login.reason == "no login");
    const web::Outcome bad = web::parse_outcome("ERR the password takes 8 to 128 printable characters");
    CHECK(!bad.ok && !bad.unknown && bad.reason == "the password takes 8 to 128 printable characters");
    const web::Outcome old = web::parse_outcome("ERR unknown command");
    CHECK(!old.ok && old.unknown);
    const web::Outcome odd = web::parse_outcome("");
    CHECK(!odd.ok && !odd.reason.empty());
}

// With --svg <file>, also writes the settings page's layout as an SVG picture.
int main(int argc, char **argv)
{
    conf_tests();
    autoload_tests();
    diag_tests();
    web_tests();
    settings_layout_tests();
    if (argc == 3 && std::string(argv[1]) == "--svg" && !write_settings_svg(argv[2]))
    {
        std::printf("FAIL: could not write %s\n", argv[2]);
        ++failures;
    }
    std::printf("%s\n", failures == 0 ? "psapp tests passed" : "psapp tests FAILED");
    return failures == 0 ? 0 : 1;
}
