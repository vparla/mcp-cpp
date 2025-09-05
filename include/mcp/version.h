//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: version.h
// Purpose: Public version API for the MCP C++ SDK (semantic version helpers).
//==========================================================================================================
#pragma once

#include <string>

namespace mcp {

struct VersionInfo {
    int major;
    int minor;
    int patch;
};

// Returns the library semantic version components
VersionInfo getVersion();

// Returns the semantic version as a string: "MAJOR.MINOR.PATCH"
std::string getVersionString();

} // namespace mcp
