//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: EnvVars.h
// Purpose: Cross-platform helpers to read environment variables safely.
//==========================================================================================================
#pragma once
#include <string>
#include <cstdlib>

inline std::string GetEnvOrDefault(const char* name, const std::string& defaultValue) {
    if (name == nullptr || *name == '\0') {
        return defaultValue;
    }
    return std::getenv(name) ? std::string(std::getenv(name)) : defaultValue;
}
