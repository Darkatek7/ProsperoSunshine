// SPDX-License-Identifier: GPL-3.0-only
// The firmware probe's report is meant to be posted: anything shaped like a MAC address (six hex pairs joined by ':' or
// '-') or an IPv4 address (four numbers up to 255) is masked before a line is written. Portable, tested in
// tests/probe_scrub_test.cpp.
#pragma once
#include <cctype>
#include <string>

namespace probe {

inline bool is_hex(char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; }
inline bool is_digit(char c) { return isdigit(static_cast<unsigned char>(c)) != 0; }

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
    return s;
}

}  // namespace probe
