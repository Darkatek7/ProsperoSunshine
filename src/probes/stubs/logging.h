// SPDX-License-Identifier: GPL-3.0-only
// Stand-in for Sunshine's logging.h when its unmodified cbs.cpp is compiled into cbs_probe: the
// BOOST_LOG(level) << ... statements of cbs.cpp go to the kernel log instead of Boost.Log.
#pragma once
#include <sstream>
#include <string>
extern "C" {
#include <ps5/klog.h>
}

namespace probe_log {
struct line {
    std::ostringstream text;
    template <class T> line &operator<<(const T &value) {
        text << value;
        return *this;
    }
    ~line() { klog_printf("ProsperoSunshine: cbs sunshine-log %s\n", text.str().c_str()); }
};
}  // namespace probe_log

#define BOOST_LOG(level) probe_log::line()
