//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Validation.h
// Purpose: Opt-in schema validation scaffolding for client/server (Off by default)
//==========================================================================================================

#pragma once

#include <string>

namespace mcp {
namespace validation {

// Validation modes for runtime shape checks (no-op by default until enabled)
enum class ValidationMode {
    Off = 0,
    Strict = 1,
};

// Utility to convert to/from string for docs/config friendliness
inline const char* toString(ValidationMode mode) {
    switch (mode) {
        case ValidationMode::Strict: return "Strict";
        case ValidationMode::Off:
        default: return "Off";
    }
}

inline ValidationMode parseMode(const std::string& s) {
    if (s == "strict" || s == "Strict") return ValidationMode::Strict;
    return ValidationMode::Off;
}

} // namespace validation
} // namespace mcp
