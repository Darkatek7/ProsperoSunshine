// SPDX-License-Identifier: GPL-3.0-only
// The Payload Manager's autoload list (itsPLK/ps5-payload-manager v0.6.3, src/autoload.c and
// src/slot.c). /data/pldmgr/autoload.txt holds one entry a line, run in order at boot after
// AUTOLOAD_DELAY (pldmgr_config.txt, when AUTOLOAD_ENABLED=1): "!<ms>" waits, anything else names a
// payload. A payload is a slot, "local/<name>" or "repo/<id>@<hash>", that is a folder under
// /data/pldmgr/payloads holding the ELF and a meta.json; the manager sends the ELF to the ELF loader
// on 127.0.0.1:9021. Absolute paths are accepted only under /data/pldmgr and USB drives, so the
// host gets a local slot of its own. A file with a line over 511 bytes, a ',' or a '\r' is refused
// as a whole by the manager, so the edits below never add one.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace psapp::autoload
{

constexpr const char *kListPath = "/data/pldmgr/autoload.txt";
constexpr const char *kConfigPath = "/data/pldmgr/pldmgr_config.txt";
constexpr const char *kSlotKey = "local/prospero-sunshine";
constexpr const char *kSlotDir = "/data/pldmgr/payloads/local/prospero-sunshine";
constexpr const char *kSlotElf = "/data/pldmgr/payloads/local/prospero-sunshine/prospero_sunshine.elf";
constexpr const char *kSlotMeta = "/data/pldmgr/payloads/local/prospero-sunshine/meta.json";

// Pure text edits of autoload.txt; `key` is a whole line.
bool contains(std::string_view list, std::string_view key);
// The list with `key` as its last entry (unchanged when it is there already).
std::string with_entry(std::string_view list, std::string_view key);
// The list without any line that is `key`; every other line stays as it was.
std::string without_entry(std::string_view list, std::string_view key);

// The manager's own switch for all of autoload (AUTOLOAD_ENABLED in pldmgr_config.txt); missing
// counts as off, as in the manager's config.c.
bool manager_autoload_enabled(std::string_view config);

// The slot's meta.json (schema 2, a local slot) for an ELF file name and an ISO-8601 time.
std::string slot_meta(std::string_view filename, std::string_view installed_at);

// Whether the host starts at boot, as far as the app can tell. The manager may keep autoload.txt to itself (mode 0600), and a
// title without /data reads nothing there: then only a running host (root) can say, and with none the app doesn't know.
enum class BootStart
{
    off,
    on,
    unknown,
};

// From what this title read of autoload.txt (`list` null when it could not, `missing` when that was because there is none) and
// the running host's answer to AUTOLOAD ("OK list=<0|1> manager=<0|1> slot=<0|1>", src/control/autoload_host.hpp; empty when
// it wasn't asked or didn't answer). The file as read wins; the host's answer only stands in for a file this title can't read.
BootStart boot_start(const std::string *list, bool missing, std::string_view host_answer);

// The home page's chip for it, and the line under the Start at boot switch while it is unknown (`host_running`: a host runs
// but did not say, an older one or one busy with a pairing).
const char *boot_chip(BootStart state);
const char *boot_unknown_hint(bool host_running);

// The report's line on the slot's copy of the host ELF when Start at boot may run it and it is another build than the data
// folder's; empty otherwise. Sizes are -1 for a missing file; `same_bytes` is empty when the bytes could not be compared (a
// title without /data, or a file it may not read), and then only the sizes count.
std::string slot_copy_note(BootStart state, long long host_size, long long slot_size, std::optional<bool> same_bytes);

} // namespace psapp::autoload
