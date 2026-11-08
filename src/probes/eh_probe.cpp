// SPDX-License-Identifier: GPL-3.0-only
// Narrows down the abort seen in boost_probe's filesystem step: do C++ exceptions unwind in a
// payload, and which Boost.Filesystem / std::filesystem call fails on /data?
#include <boost/filesystem.hpp>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
extern "C" {
#include <ps5/klog.h>
}

#define STEP(...) klog_printf("ProsperoSunshine: eh " __VA_ARGS__)

int main() {
    STEP("start\n");
    try {
        throw std::runtime_error("unwind");
    } catch (const std::exception &e) {
        STEP("std exception caught: %s\n", e.what());
    }
    std::error_code sec;
    const bool std_made = std::filesystem::create_directories("/data/prosperosunshine-eh/std", sec);
    STEP("std create_directories made=%d ec=%d %s\n", std_made, sec.value(), sec.message().c_str());
    STEP("std exists=%d\n", std::filesystem::exists("/data/prosperosunshine-eh/std", sec));
    boost::system::error_code bec;
    STEP("boost status call\n");
    const auto st = boost::filesystem::status("/data", bec);
    STEP("boost status type=%d ec=%d %s\n", static_cast<int>(st.type()), bec.value(), bec.message().c_str());
    const bool made = boost::filesystem::create_directories("/data/prosperosunshine-eh/boost", bec);
    STEP("boost create_directories made=%d ec=%d %s\n", made, bec.value(), bec.message().c_str());
    // Existing directory, with a stale errno as an earlier failed socket call would leave it.
    errno = EINPROGRESS;
    const bool again = boost::filesystem::create_directories("/data/prosperosunshine-eh/boost", bec);
    STEP("boost create_directories existing made=%d ec=%d %s\n", again, bec.value(), bec.message().c_str());
    errno = EINPROGRESS;
    const bool std_again = std::filesystem::create_directories("/data/prosperosunshine-eh/std", sec);
    STEP("std create_directories existing made=%d ec=%d %s\n", std_again, sec.value(), sec.message().c_str());
    errno = EINPROGRESS;
    const bool fresh = boost::filesystem::create_directories("/data/prosperosunshine-eh/fresh/deep", bec);
    STEP("boost create_directories new+stale errno made=%d ec=%d %s\n", fresh, bec.value(), bec.message().c_str());
    errno = EINPROGRESS;
    const bool fresh_one = boost::filesystem::create_directory("/data/prosperosunshine-eh/fresh1", bec);
    STEP("boost create_directory new+stale errno made=%d ec=%d %s\n", fresh_one, bec.value(), bec.message().c_str());
    errno = EINPROGRESS;
    const bool std_fresh = std::filesystem::create_directories("/data/prosperosunshine-eh/stdfresh/deep", sec);
    STEP("std create_directories new+stale errno made=%d ec=%d %s\n", std_fresh, sec.value(), sec.message().c_str());
    errno = EINPROGRESS;
    const bool single =boost::filesystem::create_directory("/data/prosperosunshine-eh/boost", bec);
    STEP("boost create_directory existing made=%d ec=%d %s\n", single, bec.value(), bec.message().c_str());
    try {
        boost::filesystem::file_size("/data/prosperosunshine-eh/missing");
    } catch (const boost::filesystem::filesystem_error &e) {
        STEP("boost exception caught: %s\n", e.what());
    }
    {
        std::ofstream out("/data/prosperosunshine-eh/boost/probe.txt", std::ios::binary);
        out << "hello";
        STEP("ofstream good=%d\n", static_cast<int>(out.good()));
    }
    {
        std::ifstream in("/data/prosperosunshine-eh/boost/probe.txt", std::ios::binary);
        const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        STEP("ifstream read=%s\n", back.c_str());
    }
    STEP("boost directory_iterator\n");
    int entries = 0;
    for (const auto &entry : boost::filesystem::directory_iterator("/data/prosperosunshine-eh/boost")) {
        ++entries;
        STEP("entry %s\n", entry.path().filename().c_str());
    }
    STEP("entries=%d\n", entries);
    boost::filesystem::remove_all("/data/prosperosunshine-eh", bec);
    STEP("done remove ec=%d\n", bec.value());
    return 0;
}
