// SPDX-License-Identifier: GPL-3.0-only
// The firmware probe's report is meant to be posted: anything shaped like a MAC address (six hex pairs joined by ':' or
// '-'), an IPv4 address (four numbers up to 255) or an IPv6 address (hex groups joined by ':', with "::" or eight groups)
// is masked before a line is written. The debug build's logs go through the same (src/log/log_filter.hpp). Portable,
// tested in tests/probe_scrub_test.cpp.
#pragma once
#include <cctype>
#include <string>
#include <utility>

namespace probe {

inline bool is_hex(char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; }
inline bool is_digit(char c) { return isdigit(static_cast<unsigned char>(c)) != 0; }
inline bool is_v6(char c) { return is_hex(c) || c == ':'; }

// After the IPv4 pass, so that an IPv4-mapped address (::ffff:1.2.3.4, as a dual-stack socket names a client) is one <ip>.
// Needs a hex digit and "::" or eight groups of up to four: times (12:34:56), "name :: value" and C++ names stay.
inline std::string scrub_ipv6(std::string s) {
    for (size_t i = 0; i < s.size(); i++) {
        if (!is_v6(s[i]) || (i && (is_v6(s[i - 1]) || isalnum(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '.'))) continue;
        size_t j = i;
        while (j < s.size() && is_v6(s[j])) j++;
        const std::string run = s.substr(i, j - i);
        size_t colons = 0, group = 0, longest = 0;
        bool digit = false;
        for (const char c : run) {
            if (c == ':') {
                colons++;
                group = 0;
            } else {
                digit = true;
                longest = group + 1 > longest ? group + 1 : longest;
                group++;
            }
        }
        const bool mapped = !run.empty() && run.back() == ':' && s.compare(j, 4, "<ip>") == 0;
        const bool after_ok = j == s.size() || mapped || !(isalnum(static_cast<unsigned char>(s[j])) ||
                                                          (s[j] == '.' && j + 1 < s.size() && is_digit(s[j + 1])));
        if (digit && colons >= 2 && longest <= 4 && (run.find("::") != std::string::npos || colons == 7) && after_ok) {
            s.replace(i, (mapped ? j + 4 : j) - i, "<ip>");
            i += 3;
        } else {
            i = j - 1;
        }
    }
    return s;
}

inline std::string scrub(std::string s) {
    for (size_t i = 0; i + 17 <= s.size(); i++) {
        const char sep = s[i + 2];
        if ((sep != ':' && sep != '-') || (i && is_hex(s[i - 1]))) continue;
        bool mac = true;
        for (size_t k = 0; k < 6 && mac; k++)
            mac = is_hex(s[i + 3 * k]) && is_hex(s[i + 3 * k + 1]) && (k == 5 || s[i + 3 * k + 2] == sep);
        if (mac && (i + 17 == s.size() || !is_hex(s[i + 17]))) s.replace(i, 17, "<mac>");
    }
    for (size_t i = 0; i < s.size(); i++) {
        if (!is_digit(s[i]) || (i && (is_digit(s[i - 1]) || s[i - 1] == '.'))) continue;
        size_t j = i;
        int parts = 0;
        for (; parts < 4; parts++) {
            size_t k = j;
            int value = 0;
            while (k < s.size() && is_digit(s[k]) && k - j < 3) value = value * 10 + (s[k++] - '0');
            if (k == j || value > 255) break;
            j = k;
            if (parts < 3) {
                if (j >= s.size() || s[j] != '.') break;
                ++j;
            }
        }
        // A fifth number (1.2.3.4.5) makes it something else; a full stop after it doesn't.
        const bool more = j < s.size() && (is_digit(s[j]) || (s[j] == '.' && j + 1 < s.size() && is_digit(s[j + 1])));
        if (parts == 4 && !more) s.replace(i, j - i, "<ip>");
    }
    return scrub_ipv6(std::move(s));
}

}  // namespace probe
