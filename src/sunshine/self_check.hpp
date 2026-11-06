// SPDX-License-Identifier: GPL-3.0-only
// The debug build's self-check at start: what the host needs before a client comes, as "selfcheck ..." lines in
// host_diag.log. main_ps5.cpp runs it before the services start, so never during a stream; it takes a second or two.
#pragma once

namespace prospero {

// The firmware and whether the console is a Pro; whether the ResourceArbitrator calls resolve, by NID or by the table of
// verified offsets; which of the host's ports another process holds (TCP 47984, 47989, 47990, 48010, 47995 and 47996, UDP
// 47998 to 48000 with the default base port); whether the mDNS socket comes up; and one capture opened only (OpenVideo, no
// Start, then closed) in the mode a 1080p60 H.264 client gets. Ends with "selfcheck done ms=<n> problems=<n>".
void self_check();

}  // namespace prospero
