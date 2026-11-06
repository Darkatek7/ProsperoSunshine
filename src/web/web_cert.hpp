// SPDX-License-Identifier: GPL-3.0-only
// The web page's own certificate. The host's Sunshine certificate is the one Moonlight pins at pairing, so it can't change,
// but it has no extensions and runs 20 years, which iOS turns into more than a warning (no serverAuth, over 825 days;
// PR 27). This one is self-signed too, so browsers still warn once, but it has what they expect: serverAuth, the
// console's addresses as subject alternative names and 825 days. It is made in <appdata> when missing, unreadable, close
// to expiry, or when the console's addresses changed.
#pragma once
#include <optional>
#include <string>

namespace prospero::web_cert {

struct Files {
    std::string cert, key;
};

// The certificate and key files to serve with; on failure an empty optional and the reason in `problem`.
std::optional<Files> ensure(std::string &problem);

}  // namespace prospero::web_cert
