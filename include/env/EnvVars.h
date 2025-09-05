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
#ifdef _WIN32
    char* val = nullptr;
    size_t len = 0;
    ::_dupenv_s(&val, &len, name);
    if (val == nullptr) {
        return defaultValue;
    }
    std::string result = val;
    ::free(val);
    return result;
#else
    const char* val = ::getenv(name);
    if (val == nullptr) {
        return defaultValue;
    }
    return std::string(val);
#endif
}
