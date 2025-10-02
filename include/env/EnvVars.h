//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: EnvVars.h
// Purpose: Cross-platform helpers to read environment variables safely.
//==========================================================================================================
#pragma once
#include <cstdlib>
#include <string>

//==========================================================================================================
// GetEnvOrDefault
// Purpose: Returns the value of the environment variable or a provided default when unset/empty.
// Args:
//   name: C-string name of the environment variable. When null or empty, returns defaultValue.
//   defaultValue: Value to return when the variable is not set.
// Returns:
//   std::string with the environment value (when set) or defaultValue otherwise.
//==========================================================================================================
inline std::string GetEnvOrDefault(const char* name, const std::string& defaultValue) {
    if (name == nullptr || *name == '\0') {
        return defaultValue;
    }
    return std::getenv(name) ? std::string(std::getenv(name)) : defaultValue;
}
