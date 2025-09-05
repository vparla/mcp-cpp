//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: version.cpp
// Purpose: Implements version helpers returning semantic version string.
//==========================================================================================================
#include "mcp/version.h"

#include <sstream>

namespace mcp {

VersionInfo getVersion() {
    auto v = VersionInfo{0, 1, 0};
    return v;
}

std::string getVersionString() {
    const auto v = getVersion();
    std::ostringstream oss;
    oss << v.major << "." << v.minor << "." << v.patch;
    auto s = oss.str();
    return s;
}

} // namespace mcp
