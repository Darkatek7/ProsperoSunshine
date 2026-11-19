// SPDX-License-Identifier: GPL-3.0-only
// io::read_tail (io.hpp) on its own: it needs only the C library, so the app's PC tests (app/tests/run.sh) build it.
#include "app/psapp/io.hpp"

#include <cstdio>

namespace psapp::io
{

bool read_tail(const std::string &path, std::string *out, std::size_t max_bytes)
{
    out->clear();
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    long size = -1;
    if (std::fseek(file, 0, SEEK_END) == 0)
        size = std::ftell(file);
    const long limit = static_cast<long>(max_bytes);
    const bool cut = size > limit;
    // A cut file is read from one byte before the cut: whether that byte ends a line tells whether the first line is whole.
    const long from = cut ? size - limit - 1 : 0;
    bool ok = size >= 0 && std::fseek(file, from, SEEK_SET) == 0;
    if (ok)
    {
        // The lines the host adds meanwhile wait for the next report.
        out->resize(static_cast<std::size_t>(size - from));
        out->resize(std::fread(out->data(), 1, out->size(), file));
        ok = !std::ferror(file);
    }
    std::fclose(file);
    if (!ok)
    {
        out->clear();
        return false;
    }
    if (cut)
    {
        const std::size_t newline = out->find('\n');
        out->erase(0, newline == std::string::npos ? out->size() : newline + 1);
    }
    return true;
}

} // namespace psapp::io
