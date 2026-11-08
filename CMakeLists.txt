# SPDX-License-Identifier: GPL-3.0-only
# PS5 build. Configure with the SDK's prospero-cmake (scripts/build.sh does this inside Docker);
# third-party libraries come prebuilt from scripts/deps.sh in out/deps.
cmake_minimum_required(VERSION 3.25)
project(ProsperoSunshine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
# std::jthread/stop_token sit behind libc++'s experimental gate in SDK v0.45.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
set(PROSPERO_WARNINGS -Wall -Wextra -Werror)

# The debug flavor (README, "Debug build"; scripts/build.sh with PROSPERO_DEBUG=1 puts it in out/debug): the same code and
# features, plus a "->" line before each call that may hang, a short self-check at start, debug as the default log level,
# addresses masked and floods held back in the logs, an optional remote log, and "-debug" after the version. It is built
# with DWARF line info and a link map; the ELF that ships has the line info stripped (same code, same addresses) and
# prospero_sunshine.debug.elf keeps it for addr2line.
option(PROSPERO_DEBUG "Build the debug flavor of the host" OFF)
set(PROSPERO_FLAVOR_SUFFIX "")
if(PROSPERO_DEBUG)
    add_compile_definitions(PROSPERO_DEBUG=1)
    add_compile_options(-g)
    set(PROSPERO_FLAVOR_SUFFIX "-debug")
endif()

set(PROSPERO_OUT "${CMAKE_SOURCE_DIR}/out" CACHE PATH "Where ELFs and libraries are placed")
set(PROSPERO_DEPS "${CMAKE_SOURCE_DIR}/out/deps" CACHE PATH "Prefix of the prebuilt third-party libraries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROSPERO_OUT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROSPERO_OUT}")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# Prebuilt static archives from out/deps as imported targets.
function(prospero_dep name)
    add_library(dep::${name} STATIC IMPORTED GLOBAL)
    set_target_properties(dep::${name} PROPERTIES
        IMPORTED_LOCATION "${PROSPERO_DEPS}/lib/lib${name}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PROSPERO_DEPS}/include"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PROSPERO_DEPS}/include")
endfunction()
foreach(lib ssl crypto opus enet nanors miniupnpc)
    prospero_dep(${lib})
endforeach()
set_target_properties(dep::ssl PROPERTIES INTERFACE_LINK_LIBRARIES "dep::crypto;pthread")

# Platform layer.
# ResourceArbitrator lookup by NID; never calls into the module.
add_library(prospero_ra STATIC src/platform/ps5/ra_resolve.c src/platform/ps5/diag.c)
target_include_directories(prospero_ra PUBLIC src)
target_compile_options(prospero_ra PRIVATE ${PROSPERO_WARNINGS})

add_library(prospero_capture STATIC src/platform/ps5/capture.c)
target_include_directories(prospero_capture PUBLIC src)
target_link_libraries(prospero_capture PUBLIC prospero_ra)
target_compile_options(prospero_capture PRIVATE ${PROSPERO_WARNINGS})

add_library(prospero_pad STATIC src/platform/ps5/pad.c src/platform/ps5/kbm.c)
target_include_directories(prospero_pad PUBLIC src)
target_compile_options(prospero_pad PRIVATE ${PROSPERO_WARNINGS})

add_library(prospero_core STATIC src/capture/capture_session.cpp src/platform/ps5/capture_backend.cpp
    src/platform/ps5/shared_capture.cpp)
target_include_directories(prospero_core PUBLIC src)
target_link_libraries(prospero_core PUBLIC prospero_capture)
target_compile_options(prospero_core PRIVATE ${PROSPERO_WARNINGS})

# mDNS responder (P9.2): portable packet code plus the socket loop.
add_library(prospero_mdns STATIC src/net/mdns.cpp src/platform/ps5/mdns_responder.cpp)
target_include_directories(prospero_mdns PUBLIC src)
target_link_libraries(prospero_mdns PUBLIC prospero_ra)  # the debug build's "->" line before the bind
target_compile_options(prospero_mdns PRIVATE ${PROSPERO_WARNINGS})

# Standalone payloads, one source file each: src/<dir>/<name>.cpp -> <name>.elf, with src/ on the include path.
function(prospero_payload_from dir name)
    add_executable(${name} src/${dir}/${name}.cpp)
    target_include_directories(${name} PRIVATE src)
    target_compile_options(${name} PRIVATE ${PROSPERO_WARNINGS})
    target_link_libraries(${name} PRIVATE ${ARGN})
endfunction()
# Probes and spikes (src/probes): research and test payloads that log their result to klog or a file. Not shipped.
function(prospero_probe name)
    prospero_payload_from(probes ${name} ${ARGN})
endfunction()
# Payloads that are part of the product (src/payloads): shipped in the release archive or embedded into the host.
function(prospero_payload name)
    prospero_payload_from(payloads ${name} ${ARGN})
endfunction()

# The firmware probe for unmeasured firmware, in the release archive (docs/firmware-probe.md).
prospero_payload(firmware_probe prospero_capture SceSystemService)
target_compile_definitions(firmware_probe PRIVATE FIRMWARE_PROBE_COMMIT="$ENV{PROSPERO_COMMIT}")
# Restarts the host from outside, in the release archive.
prospero_payload(host_restart)
# The host embeds it and hands it to the loader for "Restart host" on the web page.
prospero_payload(relaunch_helper)

# Probes the docs name as a tool: the firmware checklist (docs/firmware-porting.md), the notes in docs/re/, test procedures.
prospero_probe(runtime_probe)
prospero_probe(capture_probe prospero_core)
prospero_probe(capture_soak prospero_core)
prospero_probe(pad_probe prospero_pad)
prospero_probe(pad_output_probe)
prospero_probe(kbm_probe prospero_pad)
prospero_probe(pad_user_probe prospero_pad SceUserService)
prospero_probe(pad_type_probe)
prospero_probe(relisten_probe)
prospero_probe(port_hold_probe)
prospero_probe(rest_probe)
prospero_probe(avcap_spike prospero_capture)
prospero_probe(open_probe prospero_capture)
prospero_probe(hdr_spike prospero_capture)
prospero_probe(slice_rate_probe prospero_capture)
prospero_probe(hevc_spike prospero_capture)
prospero_probe(udp_burst_spike)
prospero_probe(audio_spike prospero_capture prospero_pad)
prospero_probe(dual_open_probe prospero_capture)
prospero_probe(ra_nid_probe prospero_ra)

# The host: Sunshine's protocol core (pinned, patched sources from .cache/deps/src) with the PS5
# replacements in src/sunshine. Built once Boost and FFmpeg are present in out/deps.
set(SUNSHINE_SRC "${CMAKE_SOURCE_DIR}/.cache/deps/src/sunshine" CACHE PATH "Extracted Sunshine tree")
set(MOONLIGHT_SRC "${CMAKE_SOURCE_DIR}/.cache/deps/src/moonlight-common-c" CACHE PATH "moonlight-common-c tree")
if(EXISTS "${PROSPERO_DEPS}/include/boost" AND EXISTS "${PROSPERO_DEPS}/lib/libavcodec.a")
    file(GLOB BOOST_LIBS "${PROSPERO_DEPS}/lib/libboost_*.a")
    set(SUNSHINE_KEPT
        nvhttp.cpp httpcommon.cpp rtsp.cpp stream.cpp crypto.cpp network.cpp audio.cpp input.cpp config.cpp
        logging.cpp cbs.cpp file_handler.cpp globals.cpp stat_trackers.cpp)
    list(TRANSFORM SUNSHINE_KEPT PREPEND "${SUNSHINE_SRC}/src/")
    # Cover art for the single app, embedded so the host can install it without a separate upload.
    set(COVER_PNG "${CMAKE_SOURCE_DIR}/assets/ps5_screen.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${COVER_PNG}")
    file(READ "${COVER_PNG}" COVER_HEX HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," COVER_BYTES "${COVER_HEX}")
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/cover_png.cpp" CONTENT
        "// Generated from assets/ps5_screen.png by CMakeLists.txt\n#include <cstddef>\nextern const unsigned char prospero_cover_png[] = {@COVER_BYTES@};\nextern const std::size_t prospero_cover_png_size = sizeof prospero_cover_png;\n")
    # The web page's files (src/web/page), embedded the same way; web_server.cpp serves them by path.
    set(WEB_PAGE_FILES index.html text/html app.js text/javascript style.css text/css)
    set(WEB_ARRAYS "")
    set(WEB_TABLE "")
    set(WEB_INDEX 0)
    list(LENGTH WEB_PAGE_FILES WEB_PAGE_LENGTH)
    while(WEB_INDEX LESS WEB_PAGE_LENGTH)
        list(GET WEB_PAGE_FILES ${WEB_INDEX} WEB_NAME)
        math(EXPR WEB_TYPE_INDEX "${WEB_INDEX} + 1")
        list(GET WEB_PAGE_FILES ${WEB_TYPE_INDEX} WEB_TYPE)
        set(WEB_FILE "${CMAKE_SOURCE_DIR}/src/web/page/${WEB_NAME}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WEB_FILE}")
        file(READ "${WEB_FILE}" WEB_HEX HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," WEB_BYTES "${WEB_HEX}")
        string(APPEND WEB_ARRAYS "static const unsigned char web_${WEB_INDEX}[] = {${WEB_BYTES}};
")
        string(APPEND WEB_TABLE "    {\"/${WEB_NAME}\", \"${WEB_TYPE}; charset=utf-8\", web_${WEB_INDEX}, sizeof web_${WEB_INDEX}},
")
        math(EXPR WEB_INDEX "${WEB_INDEX} + 2")
    endwhile()
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/web_page.cpp" CONTENT
        "// Generated from src/web/page by CMakeLists.txt
#include <cstddef>
struct prospero_web_asset {
    const char *path, *type;
    const unsigned char *data;
    std::size_t size;
};
@WEB_ARRAYS@extern const prospero_web_asset prospero_web_assets[] = {
@WEB_TABLE@};
extern const std::size_t prospero_web_asset_count = sizeof prospero_web_assets / sizeof prospero_web_assets[0];
")
    # The relaunch helper payload, embedded for "Restart host" on the web page (src/payloads/relaunch_helper.cpp).
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/relaunch_helper_elf.cpp"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_FILE:relaunch_helper>"
            "-DOUTPUT=${CMAKE_BINARY_DIR}/generated/relaunch_helper_elf.cpp" -DSYMBOL=prospero_relaunch_helper
            -P "${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake"
        DEPENDS relaunch_helper "${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake")
    add_executable(prospero_sunshine "${CMAKE_BINARY_DIR}/generated/cover_png.cpp"
        "${CMAKE_BINARY_DIR}/generated/relaunch_helper_elf.cpp"
        "${CMAKE_BINARY_DIR}/generated/web_page.cpp" src/web/web_server.cpp src/web/web_login.cpp src/web/web_report.cpp
        src/web/web_cert.cpp src/net/upnp_client.cpp
        app/src/psapp/conf.cpp app/src/psapp/diag.cpp
        ${SUNSHINE_KEPT}
        "${MOONLIGHT_SRC}/src/RtspParser.c"
        src/sunshine/main_ps5.cpp src/sunshine/video_ps5.cpp src/sunshine/process_ps5.cpp
        src/sunshine/platform_ps5.cpp src/sunshine/stubs_ps5.cpp src/sunshine/self_check.cpp src/control/control_server.cpp
        src/control/autoload_host.cpp src/control/file_bridge.cpp app/src/psapp/autoload.cpp src/apps/titles.cpp src/platform/ps5/title_launch.c
        src/platform/ps5/console_users.c src/platform/ps5/profile_seat.c src/platform/ps5/user_service.c)
    # Sunshine includes its own headers as "src/..." and "..."; FFmpeg's internal headers include a
    # "config.h" that must not resolve to Sunshine's, hence -idirafter for the Sunshine tree.
    target_include_directories(prospero_sunshine PRIVATE src "${SUNSHINE_SRC}" "${CMAKE_BINARY_DIR}/third-party")
    target_include_directories(prospero_sunshine SYSTEM PRIVATE "${PROSPERO_DEPS}/include" "${PROSPERO_DEPS}/include/nanors")
    target_compile_options(prospero_sunshine PRIVATE "SHELL:-idirafter ${SUNSHINE_SRC}/src")
    # Sunshine expects moonlight-common-c headers under <moonlight-common-c/src/...>.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/third-party")
    file(CREATE_LINK "${MOONLIGHT_SRC}" "${CMAKE_BINARY_DIR}/third-party/moonlight-common-c" SYMBOLIC)
    # The autoload list's text edits are the app's; it includes them as "app/psapp/...".
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/third-party/app")
    file(CREATE_LINK "${CMAKE_SOURCE_DIR}/app/src/psapp" "${CMAKE_BINARY_DIR}/third-party/app/psapp" SYMBOLIC)
    # scripts/build.sh finds the commit; host_diag.log and the app's report name it.
    set(PROSPERO_COMMIT "$ENV{PROSPERO_COMMIT}")
    if(NOT PROSPERO_COMMIT)
        set(PROSPERO_COMMIT "unknown")
    endif()
    target_compile_definitions(prospero_sunshine PRIVATE
        PROJECT_NAME="ProsperoSunshine" PROJECT_VERSION="0.5.0${PROSPERO_FLAVOR_SUFFIX}" PROJECT_VERSION_MAJOR="0"
        PROJECT_VERSION_MINOR="5" PROJECT_VERSION_PATCH="0" PROJECT_VERSION_COMMIT="${PROSPERO_COMMIT}"
        PROJECT_FQDN="dev.prosperosunshine" SUNSHINE_ASSETS_DIR="/data/prosperosunshine/assets"
        SUNSHINE_PUBLISHER_NAME="ProsperoSunshine" SUNSHINE_PUBLISHER_WEBSITE=""
        SUNSHINE_PUBLISHER_ISSUE_URL="" SUNSHINE_TRAY=0 BOOST_ALL_NO_LIB)
    target_link_libraries(prospero_sunshine PRIVATE prospero_core prospero_pad prospero_mdns
        "-Wl,--start-group" ${BOOST_LIBS} "${PROSPERO_DEPS}/lib/libdisplaydevice_common.a"
        "${PROSPERO_DEPS}/lib/libavcodec.a" "${PROSPERO_DEPS}/lib/libswscale.a" "${PROSPERO_DEPS}/lib/libavutil.a"
        dep::ssl dep::crypto dep::opus dep::enet dep::nanors dep::miniupnpc "-Wl,--end-group" pthread SceSystemService
        SceUserService)
    if(PROSPERO_DEBUG)
        target_link_options(prospero_sunshine PRIVATE "-Wl,-Map=${PROSPERO_OUT}/prospero_sunshine.map")
        add_custom_command(TARGET prospero_sunshine POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${PROSPERO_OUT}/prospero_sunshine.elf" "${PROSPERO_OUT}/prospero_sunshine.debug.elf"
            COMMAND "${CMAKE_STRIP}" --strip-debug "${PROSPERO_OUT}/prospero_sunshine.elf"
            COMMENT "prospero_sunshine.debug.elf keeps the line info; the ELF that ships has it stripped")
    endif()
endif()
