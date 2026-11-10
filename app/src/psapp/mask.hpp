// SPDX-License-Identifier: GPL-3.0-only
// The one way this project hides where a posted file comes from. The diagnostic report (diag.cpp), the logs of the debug build
// (src/log/log_filter.hpp) and the firmware probe's report (src/probe_scrub.hpp) all use it, so a stand-in means the same
// everywhere:
//   - An address becomes a tag that still says what it was and, within one Masker, is the same for the same address:
//     <lan-1> (private ranges), <tailscale-1> (100.64.0.0/10 and Tailscale's IPv6 range, which carrier NATs share), <link-1>
//     (link-local), <public-1> (everything else). IPv6 gets a 6 behind the word (<lan6-1>); an IPv4-mapped IPv6 address is the
//     IPv4 address it carries. A MAC address becomes <mac-1>. Loopback, 0.0.0.0, multicast, broadcast and :: stay: they name
//     nobody and the host's own logic reads them (a request from 127.0.0.1 is a Tailscale forward, Host: 0.0.0.0 asks for low
//     audio quality).
//   - Scope::all also hides the host name of a URL (rtsp://deck.local:48010) and of a Host header, <host-1>, and the user:password
//     of a URL. Scope::internet leaves everything of the home as it is and hides only <public-N> addresses: what the normal
//     build writes to its logs.
//   - stand_in(category, key) gives the same numbered tag to the same key for anything else (names, ids).
// Numbers count from 1 in each Masker, that is in each run of the host or each report; a file of an earlier run may use them for
// other values. A Masker remembers at most `limit` values; past that new ones get the tag without a number.
// Header-only and portable (C++17), tested in tests/mask_test.cpp.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psapp::mask
{

enum class Scope
{
    all,      // every address that isn't special, host names of URLs and Host headers
    internet, // public addresses only
};

namespace detail
{

inline bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}
inline bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline bool is_v6(char c)
{
    return is_hex(c) || c == ':';
}
inline bool is_alnum(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
inline std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = lower(c);
    return out;
}
inline int hex_value(char c)
{
    return is_digit(c) ? c - '0' : lower(c) - 'a' + 10;
}

// "1.2.3.4" at `at`: four numbers up to 255 of at most three digits, not followed by a digit or a full stop and a digit (a fifth
// number makes it something else). `end` is one past it.
inline bool parse_v4(std::string_view s, std::size_t at, std::size_t &end, std::array<std::uint8_t, 4> &out)
{
    std::size_t j = at;
    for (int part = 0; part < 4; ++part)
    {
        std::size_t k = j;
        int value = 0;
        while (k < s.size() && is_digit(s[k]) && k - j < 3)
            value = value * 10 + (s[k++] - '0');
        if (k == j || value > 255)
            return false;
        out[static_cast<std::size_t>(part)] = static_cast<std::uint8_t>(value);
        j = k;
        if (part < 3)
        {
            if (j >= s.size() || s[j] != '.')
                return false;
            ++j;
        }
    }
    if (j < s.size() && (is_digit(s[j]) || (s[j] == '.' && j + 1 < s.size() && is_digit(s[j + 1]))))
        return false;
    end = j;
    return true;
}

// The groups of one side of "::" (or of a whole address): 1 to 4 hex digits each, the last one of the address may be a dotted quad
// that makes two.
inline bool parse_groups(std::string_view part, bool allow_v4_last, std::vector<std::uint16_t> &out)
{
    if (part.empty())
        return true;
    std::size_t at = 0;
    while (true)
    {
        const std::size_t colon = part.find(':', at);
        const std::string_view piece = part.substr(at, colon == std::string_view::npos ? std::string_view::npos : colon - at);
        if (piece.empty())
            return false;
        if (piece.find('.') != std::string_view::npos)
        {
            std::size_t end = 0;
            std::array<std::uint8_t, 4> v4{};
            if (colon != std::string_view::npos || !allow_v4_last || !parse_v4(piece, 0, end, v4) || end != piece.size())
                return false;
            out.push_back(static_cast<std::uint16_t>(v4[0] << 8 | v4[1]));
            out.push_back(static_cast<std::uint16_t>(v4[2] << 8 | v4[3]));
        }
        else
        {
            if (piece.size() > 4)
                return false;
            unsigned value = 0;
            for (const char c : piece)
            {
                if (!is_hex(c))
                    return false;
                value = value * 16 + static_cast<unsigned>(hex_value(c));
            }
            out.push_back(static_cast<std::uint16_t>(value));
        }
        if (colon == std::string_view::npos)
            return true;
        at = colon + 1;
    }
}

inline bool parse_v6(std::string_view token, std::array<std::uint8_t, 16> &out)
{
    const std::size_t gap = token.find("::");
    if (gap != std::string_view::npos && token.find("::", gap + 1) != std::string_view::npos)
        return false;
    std::vector<std::uint16_t> left, right;
    if (gap == std::string_view::npos)
    {
        if (!parse_groups(token, true, left) || left.size() != 8)
            return false;
    }
    else
    {
        const std::string_view tail = token.substr(gap + 2);
        if (!parse_groups(token.substr(0, gap), false, left) || !parse_groups(tail, true, right) || left.size() + right.size() > 7)
            return false;
    }
    std::array<std::uint16_t, 8> groups{};
    for (std::size_t i = 0; i < left.size(); ++i)
        groups[i] = left[i];
    for (std::size_t i = 0; i < right.size(); ++i)
        groups[8 - right.size() + i] = right[i];
    for (std::size_t i = 0; i < 8; ++i)
    {
        out[2 * i] = static_cast<std::uint8_t>(groups[i] >> 8);
        out[2 * i + 1] = static_cast<std::uint8_t>(groups[i] & 0xff);
    }
    return true;
}

enum class Kind
{
    special,
    lan,
    tailscale,
    link,
    internet,
};

inline Kind classify_v4(const std::array<std::uint8_t, 4> &b)
{
    if (b[0] == 0 || b[0] == 127 || b[0] >= 224)
        return Kind::special;
    if (b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168))
        return Kind::lan;
    if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
        return Kind::tailscale;
    if (b[0] == 169 && b[1] == 254)
        return Kind::link;
    return Kind::internet;
}

inline Kind classify_v6(const std::array<std::uint8_t, 16> &b)
{
    if (b[0] == 0xff)
        return Kind::special;
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
        return Kind::link;
    if ((b[0] & 0xfe) == 0xfc)
        return b[0] == 0xfd && b[1] == 0x7a && b[2] == 0x11 && b[3] == 0x5c && b[4] == 0xa1 && b[5] == 0xe0 ? Kind::tailscale : Kind::lan;
    return Kind::internet;
}

inline const char *kind_word(Kind kind)
{
    switch (kind)
    {
    case Kind::lan:
        return "lan";
    case Kind::tailscale:
        return "tailscale";
    case Kind::link:
        return "link";
    default:
        return "public";
    }
}

inline std::string v4_text(const std::array<std::uint8_t, 4> &b)
{
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]);
}

// A host name: letters, digits, '.', '_' and '-'. An all-digit-and-dot name is an IPv4 address, which the address pass handles.
inline std::size_t host_length(std::string_view s)
{
    std::size_t n = 0;
    while (n < s.size() && (is_alnum(s[n]) || s[n] == '.' || s[n] == '_' || s[n] == '-'))
        ++n;
    return n;
}
inline bool numeric_host(std::string_view host)
{
    return std::all_of(host.begin(), host.end(), [](char c) { return is_digit(c) || c == '.'; });
}

} // namespace detail

// A PEM block (BEGIN to END, or to the end when the text is cut off) becomes <certificate>: a certificate or a key is never posted.
inline std::string hide_pem(std::string text)
{
    for (std::size_t begin; (begin = text.find("-----BEGIN ")) != std::string::npos;)
    {
        std::size_t end = text.find("-----END ", begin);
        if (end != std::string::npos)
            end = text.find("-----", end + 9);
        end = end == std::string::npos ? text.size() : end + 5;
        text.replace(begin, end - begin, "<certificate>");
    }
    return text;
}

class Masker
{
public:
    explicit Masker(Scope scope = Scope::all, std::size_t limit = 256) : scope_(scope), limit_(limit) {}
    Masker(const Masker &) = delete;
    Masker &operator=(const Masker &) = delete;

    Scope scope() const { return scope_; }

    // `text` with its addresses (and, in Scope::all, its host names) replaced.
    std::string apply(std::string_view text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scope_ == Scope::all)
            return addresses(hosts(text));
        return addresses(text);
    }

    // The tag of `key` in `category`, "<category-N>": the same for the same key. For names, ids and what else a caller hides.
    std::string stand_in(std::string_view category, std::string_view key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stand_in_locked(category, key);
    }

    // `text` already holds tags of an earlier Masker (a log the host masked): later numbers continue after them, so one tag
    // never names two things in a file that holds both.
    void reserve(std::string_view text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t at = text.find('<'); at != std::string_view::npos; at = text.find('<', at + 1))
        {
            std::size_t i = at + 1;
            while (i < text.size() && ((text[i] >= 'a' && text[i] <= 'z') || detail::is_digit(text[i])))
                ++i;
            if (i == at + 1 || i >= text.size() || text[i] != '-')
                continue;
            const std::string word(text.substr(at + 1, i - at - 1));
            std::size_t j = i + 1;
            unsigned number = 0;
            while (j < text.size() && detail::is_digit(text[j]) && number < 100000000u)
                number = number * 10 + static_cast<unsigned>(text[j++] - '0');
            if (j == i + 1 || j >= text.size() || text[j] != '>')
                continue;
            unsigned &next = next_[word];
            next = std::max(next, number);
        }
    }

private:
    std::string stand_in_locked(std::string_view category, std::string_view key)
    {
        std::string full(category);
        full += '\0';
        full += key;
        const auto it = seen_.find(full);
        if (it != seen_.end())
            return it->second;
        if (seen_.size() >= limit_)
            return "<" + std::string(category) + ">";
        unsigned &number = next_[std::string(category)];
        ++number;
        std::string tag = "<" + std::string(category) + "-" + std::to_string(number) + ">";
        seen_.emplace(std::move(full), tag);
        return tag;
    }

    // What an address of this kind becomes; `original` when it stays.
    std::string tag_for(detail::Kind kind, bool v6, const std::string &key, std::string_view original)
    {
        if (kind == detail::Kind::special || (scope_ == Scope::internet && kind != detail::Kind::internet))
            return std::string(original);
        return stand_in_locked(std::string(detail::kind_word(kind)) + (v6 ? "6" : ""), key);
    }

    std::string v4_tag(const std::array<std::uint8_t, 4> &b, std::string_view original)
    {
        return tag_for(detail::classify_v4(b), false, detail::v4_text(b), original);
    }

    std::string v6_tag(const std::array<std::uint8_t, 16> &b, std::string_view original)
    {
        // ::ffff:1.2.3.4 and the old ::1.2.3.4 are the IPv4 address: the same host keeps one tag.
        bool zero_head = true;
        for (std::size_t i = 0; i < 10; ++i)
            zero_head = zero_head && b[i] == 0;
        const bool mapped = zero_head && b[10] == 0xff && b[11] == 0xff;
        const bool compatible = zero_head && b[10] == 0 && b[11] == 0;
        if (mapped || compatible)
            return v4_tag({b[12], b[13], b[14], b[15]}, original);
        std::string key;
        for (const std::uint8_t byte : b)
            key += static_cast<char>(byte);
        return tag_for(detail::classify_v6(b), true, key, original);
    }

    // A MAC address: six pairs of hex digits joined by ':' or '-'.
    static bool mac_at(std::string_view s, std::size_t i)
    {
        if (i + 17 > s.size())
            return false;
        const char sep = s[i + 2];
        if ((sep != ':' && sep != '-') || (i && detail::is_hex(s[i - 1])) || (i >= 3 && s[i - 1] == sep && detail::is_hex(s[i - 2])))
            return false;
        for (std::size_t k = 0; k < 6; ++k)
            if (!detail::is_hex(s[i + 3 * k]) || !detail::is_hex(s[i + 3 * k + 1]) || (k < 5 && s[i + 3 * k + 2] != sep))
                return false;
        // Not hex after it, and not a seventh pair (an 8-byte id).
        return i + 17 == s.size() || !(detail::is_hex(s[i + 17]) || (s[i + 17] == sep && i + 18 < s.size() && detail::is_hex(s[i + 18])));
    }

    // An IPv6 address at `i`: a run of hex digits and ':' that parses, with "::" or eight groups (times such as 12:34:56, "name ::
    // value" and C++ names stay), not part of a longer word; it may end in a dotted quad. Returns its length, 0 when it isn't one.
    std::size_t v6_at(std::string_view s, std::size_t i, std::string &replacement)
    {
        if (!detail::is_v6(s[i]) || (i && (detail::is_v6(s[i - 1]) || detail::is_alnum(s[i - 1]) || s[i - 1] == '.')))
            return 0;
        std::size_t j = i;
        while (j < s.size() && detail::is_v6(s[j]))
            ++j;
        const auto try_token = [&](std::size_t end, std::size_t tail_end) -> std::size_t {
            // tail_end: where the token (with a dotted quad) really ends; the character after it decides whether it stands alone.
            const std::string_view token = s.substr(i, end - i);
            std::array<std::uint8_t, 16> bytes{};
            const std::size_t colons = static_cast<std::size_t>(std::count(token.begin(), token.end(), ':'));
            const bool digit = std::any_of(token.begin(), token.end(), [](char c) { return c != ':' && c != '.'; });
            if (!digit || colons < 2 || !detail::parse_v6(token, bytes))
                return 0;
            const bool after_ok =
                tail_end == s.size() || !(detail::is_alnum(s[tail_end]) || (s[tail_end] == '.' && tail_end + 1 < s.size() && detail::is_digit(s[tail_end + 1])));
            if (!after_ok)
                return 0;
            replacement = v6_tag(bytes, token);
            return end - i;
        };
        // A dotted quad at the end: the run stopped at its first '.', the quad starts after the last ':'.
        if (j < s.size() && s[j] == '.')
        {
            const std::size_t colon = s.rfind(':', j - 1);
            std::array<std::uint8_t, 4> v4{};
            std::size_t end = 0;
            if (colon != std::string_view::npos && colon >= i && colon + 1 < j && detail::parse_v4(s, colon + 1, end, v4))
                return try_token(end, end);
        }
        if (const std::size_t used = try_token(j, j))
            return used;
        // "address fe80::1: failed": a single colon after the address belongs to the sentence.
        std::size_t end = j;
        if (j - i >= 2 && s[j - 1] == ':' && s[j - 2] != ':')
        {
            end = j - 1;
            if (const std::size_t used = try_token(end, end))
                return used;
        }
        // Sunshine writes the peer of a connection as "<address>:<port>", without brackets: when the whole run is no address, a last
        // group of up to five decimal digits may be the port, and the address is what comes before it.
        const std::size_t last = end == 0 ? std::string_view::npos : s.rfind(':', end - 1);
        if (last != std::string_view::npos && last > i && end - last - 1 >= 1 && end - last - 1 <= 5 &&
            std::all_of(s.begin() + static_cast<std::ptrdiff_t>(last) + 1, s.begin() + static_cast<std::ptrdiff_t>(end), [](char c) { return detail::is_digit(c); }))
            return try_token(last, last);
        return 0;
    }

    std::string addresses(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        std::size_t i = 0;
        while (i < s.size())
        {
            if (scope_ == Scope::all && mac_at(s, i))
            {
                std::string key = detail::lowered(s.substr(i, 17));
                std::replace(key.begin(), key.end(), '-', ':');
                out += stand_in_locked("mac", key);
                i += 17;
                continue;
            }
            std::string replacement;
            if (const std::size_t used = v6_at(s, i, replacement))
            {
                out += replacement;
                i += used;
                continue;
            }
            if (detail::is_digit(s[i]) && (i == 0 || !(detail::is_digit(s[i - 1]) || s[i - 1] == '.')))
            {
                std::array<std::uint8_t, 4> b{};
                std::size_t end = 0;
                if (detail::parse_v4(s, i, end, b))
                {
                    out += v4_tag(b, s.substr(i, end - i));
                    i = end;
                    continue;
                }
            }
            out += s[i++];
        }
        return out;
    }

    // The host name in "Host -- x", "Host :: x", "Host: x" (a header as Sunshine dumps it, or in a raw RTSP message) and in
    // "Found Host: x".
    std::string host_line(std::string line)
    {
        std::size_t value = std::string::npos;
        constexpr std::string_view found = "Found Host: ";
        if (line.compare(0, found.size(), found) == 0)
            value = found.size();
        else
            for (const std::string_view separator : {std::string_view(" -- "), std::string_view(" :: "), std::string_view(": ")})
            {
                const std::size_t at = line.find(separator);
                if (at == std::string::npos)
                    continue;
                const std::string name = detail::lowered(std::string_view(line).substr(0, at));
                if (name != "host" && name != "x-forwarded-host")
                    continue;
                // "Host: x" is also how the host's own report starts a line ("Host: running, version 0.6.0"): there only a
                // value that is nothing but a name with a dot or a port counts (a raw RTSP header is one).
                if (separator == ": ")
                {
                    std::string_view rest = std::string_view(line).substr(at + separator.size());
                    while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' '))
                        rest.remove_suffix(1);
                    const std::size_t length = detail::host_length(rest);
                    const std::string_view port = rest.substr(length);
                    const bool port_ok = port.empty() || (port.size() > 1 && port[0] == ':' && std::all_of(port.begin() + 1, port.end(), [](char c) { return detail::is_digit(c); }));
                    if (!port_ok || (rest.substr(0, length).find('.') == std::string_view::npos && port.empty()))
                        continue;
                }
                value = at + separator.size();
                break;
            }
        if (value == std::string::npos)
            return line;
        return line.substr(0, value) + host_at(std::string_view(line).substr(value));
    }

    // `text` starts with a host name (and maybe ":port"): the name replaced unless it is an address, a tag or empty.
    std::string host_at(std::string_view text)
    {
        const std::size_t length = detail::host_length(text);
        const std::string_view name = text.substr(0, length);
        if (name.empty() || detail::numeric_host(name))
            return std::string(text);
        return stand_in_locked("host", detail::lowered(name)) + std::string(text.substr(length));
    }

    // "scheme://user:password@host:port/": the user part and the host name replaced.
    std::string urls(std::string line)
    {
        std::string out;
        std::size_t at = 0;
        for (std::size_t found = line.find("://"); found != std::string::npos; found = line.find("://", at))
        {
            out.append(line, at, found + 3 - at);
            at = found + 3;
            if (found == 0 || !detail::is_alnum(line[found - 1]))
                continue;
            std::size_t end = at;
            while (end < line.size() && std::string_view("/?# \t\r\n\"'<>),").find(line[end]) == std::string_view::npos)
                ++end;
            const std::size_t sign = line.rfind('@', end - 1);
            std::size_t host_start = at;
            if (sign != std::string::npos && sign >= at && sign < end)
            {
                out += "<userinfo>@";
                host_start = sign + 1;
            }
            if (host_start < end && line[host_start] != '[')
                out += host_at(std::string_view(line).substr(host_start, end - host_start));
            else
                out.append(line, host_start, end - host_start);
            at = end;
        }
        out.append(line, at, std::string::npos);
        return out;
    }

    std::string hosts(std::string_view text)
    {
        std::string out;
        std::size_t at = 0;
        while (at <= text.size())
        {
            std::size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
                end = text.size();
            out += urls(host_line(std::string(text.substr(at, end - at))));
            if (end < text.size())
                out += '\n';
            at = end + 1;
        }
        return out;
    }

    Scope scope_;
    std::size_t limit_;
    std::mutex mutex_;
    std::map<std::string, std::string> seen_;
    std::map<std::string, unsigned> next_;
};

} // namespace psapp::mask
