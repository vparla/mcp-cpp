//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Logger.cpp
// Purpose: Definitions for Logger static members.
//==========================================================================================================

#include "logging/Logger.h"

// Define static members
LogLevel Logger::sLogLevel = LogLevel::LOG_INFO_LEVEL;
std::ofstream Logger::sLogFile;
std::mutex Logger::sLogMutex;
