// SPDX-License-Identifier: GPL-3.0-only
// Which flavor this is (README, "Debug build"). The host compiles this with its CMake flavor (PROSPERO_DEBUG) for the web
// page; app/build.sh defines PROSPERO_DEBUG in its staged copy when it builds PPSA47989-debug.zip, which bundles the debug
// host.
#pragma once

namespace psapp
{

#ifdef PROSPERO_DEBUG
inline constexpr bool kDebugBuild = true;
#else
inline constexpr bool kDebugBuild = false;
#endif

} // namespace psapp
