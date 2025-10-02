//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: version.h
// Purpose: Public version API for the MCP C++ SDK (semantic version helpers).
//==========================================================================================================
#pragma once

#include <string>

namespace mcp {

//==========================================================================================================
// VersionInfo
// Purpose: Semantic version components.
// Fields:
//   major, minor, patch: Version components.
//==========================================================================================================
struct VersionInfo {
    int major;
    int minor;
    int patch;
};

//==========================================================================================================
// getVersion
// Purpose: Returns the library semantic version components.
// Args:
//   (none)
// Returns:
//   VersionInfo {major, minor, patch}
//==========================================================================================================
VersionInfo getVersion();

//==========================================================================================================
// getVersionString
// Purpose: Returns the semantic version string.
// Args:
//   (none)
// Returns:
//   std::string formatted as "MAJOR.MINOR.PATCH"
//==========================================================================================================
std::string getVersionString();

} // namespace mcp
