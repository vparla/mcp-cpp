//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Logger.h
// Purpose: Logging and HRESULT helpers; to be made fully cross-platform in Step 2.
//==========================================================================================================
#pragma once

#ifdef _WIN32
#  include <windows.h>
#endif
#include <mutex>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <string.h>
#include <cstring>
#include <errno.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <format>
#include <cstdlib>
#include <cctype>
#include "env/EnvVars.h"

#ifdef _WIN32
// Helper to convert std::wstring to UTF-8 std::string (Windows only)
inline std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(sizeNeeded, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

// Helper to convert UTF-8 std::string to std::wstring (Windows only)
inline std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return {};
    int sizeNeeded = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring wstrTo(sizeNeeded, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstrTo[0], sizeNeeded);
    return wstrTo;
}
#endif

// Log level enum
enum class LogLevel {
    LOG_DEBUG_LEVEL,
    LOG_INFO_LEVEL,
    LOG_WARN_LEVEL,
    LOG_ERROR_LEVEL,
    LOG_FATAL_LEVEL
};

class Logger {
public:
    // Severity level scoped to Logger
    enum class Level {
        DEBUG = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3,
        FATAL = 4
    };

    // Convert common level strings to Logger::Level (case-insensitive). Defaults to DEBUG.
    static Level levelFromString(const std::string& lvl) {
        std::string s; s.reserve(lvl.size());
        for (char c : lvl) s.push_back(static_cast<char>(::toupper(static_cast<unsigned char>(c))));
        if (s == "DEBUG") return Level::DEBUG;
        if (s == "INFO")  return Level::INFO;
        if (s == "WARN" || s == "WARNING")  return Level::WARN;
        if (s == "ERROR") return Level::ERROR;
        if (s == "FATAL") return Level::FATAL;
        return Level::DEBUG;
    }
    // Variadic logging using C++20 std::vformat with runtime format strings
    template <typename... Args>
    static void logf(const char* level, const char* fmt, const char* file, unsigned int line, Args&&... args) {
        std::string buffer;
        try {
            buffer = std::vformat(fmt, std::make_format_args(args...));
        } catch (const std::format_error& e) {
            buffer = std::format("Format error: {}", e.what());
        }

        // Only append Windows error message for ERROR or FATAL logs
#ifdef _WIN32
        if ((::strncmp(level, "ERROR", 5) == 0 || ::strncmp(level, "FATAL", 5) == 0)) {
            DWORD lastErr = ::GetLastError();
            ::SetLastError(0);
            if (lastErr != 0) {
                std::string errMsg = ::WStringToUtf8(Logger::GetErrorMessage(lastErr));
                if (!errMsg.empty()) {
                    buffer += " | WinErr: " + errMsg;
                }
            }
        }
#endif

        log(level, buffer, file, line);
    }
    
#ifdef _WIN32
    // Helper to format Windows error codes with improved HRESULT handling
    static std::wstring GetErrorMessage(HRESULT hr) {
        LPWSTR messageBuffer = nullptr;
        DWORD size = 0;
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
        
        // Try to get message from system first
        flags |= FORMAT_MESSAGE_FROM_SYSTEM;
        size = ::FormatMessageW(flags, NULL, hr, 
                              MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
                              (LPWSTR)&messageBuffer, 0, NULL);
        
        // If system doesn't have the message, try COM error messages
        if (size == 0 && HRESULT_FACILITY(hr) == FACILITY_ITF) {
            // Reset flags to try COM module
            flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE;
            HMODULE hModule = ::LoadLibraryEx(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (hModule) {
                size = ::FormatMessageW(flags, hModule, hr, 
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
                                      (LPWSTR)&messageBuffer, 0, NULL);
                ::FreeLibrary(hModule);
            }
        }
        
        std::wstring message;
        if (size > 0 && messageBuffer) {
            // Remove trailing whitespace, newlines and carriage returns
            message = std::wstring(messageBuffer);
            size_t pos = message.find_last_not_of(L" \t\r\n");
            if (pos != std::wstring::npos) {
                message.erase(pos + 1);
            }
        } else {
            // If we still don't have a message, use a generic one based on the facility
            switch (HRESULT_FACILITY(hr)) {
                case FACILITY_ITF:    message = L"Interface-specific error"; break;
                case FACILITY_WIN32:  message = L"Win32 error"; break;
                case FACILITY_WINDOWS: message = L"Windows subsystem error"; break;
                case FACILITY_STORAGE: message = L"Storage error"; break;
                case FACILITY_RPC:    message = L"RPC error"; break;
                case FACILITY_SSPI:   message = L"Security error"; break;
                default:             message = L"Unknown error";
            }
        }
        
        if (messageBuffer) {
            ::LocalFree(messageBuffer);
        }
        return message;
    }
#endif

public:
    // Configure logging
    static void setLogLevel(LogLevel level) {
        sLogLevel = level;
    }
    
    static void setLogFile(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(sLogMutex);
        if (sLogFile.is_open()) {
            sLogFile.close();
        }
        sLogFile.open(filePath, std::ios::out | std::ios::app);
        if (!sLogFile.is_open()) {
#ifdef _WIN32
            DWORD err = ::GetLastError();
            std::wstring msg = Utf8ToWString(std::string("Failed to open log file: ") + filePath +
                               std::string(" (error code ") + std::to_string(err) + std::string(")"));
            HANDLE hEventSource = ::RegisterEventSourceW(NULL, L"BasicService");
            if (hEventSource) {
                LPCWSTR strings[1] = { msg.c_str() };
                ::ReportEventW(hEventSource, EVENTLOG_ERROR_TYPE, 0, 0, NULL, 1, 0, strings, NULL);
                ::DeregisterEventSource(hEventSource);
            }
#else
            std::cerr << "[ERROR] Failed to open log file: " << filePath << " (errno=" << errno << ")" << std::endl;
#endif
        } else {
            // Write a newline and timestamp as the first entry
            if (sLogFile.is_open()) {
                auto now = std::chrono::system_clock::now();
                std::time_t now_time = std::chrono::system_clock::to_time_t(now);
                std::tm buf{};
                #ifdef _WIN32
                ::localtime_s(&buf, &now_time);
                #else
                ::localtime_r(&now_time, &buf);
                #endif
                sLogFile << "\n=== Log opened at " << std::put_time(&buf, "%Y-%m-%d %H:%M:%S") << " ===\n";
                sLogFile.flush();
            }
        }
    }
    
    static void log(const char* level, const std::string& msg, const char* file, unsigned int line) {
        std::lock_guard<std::mutex> lock(sLogMutex);
        std::ostringstream oss;
        // Optional ANSI colorization for LABEL only controlled by MCP_LOG_COLOR
        static bool colorEnabled = [](){
            const std::string v = GetEnvOrDefault("MCP_LOG_COLOR", "1");
            return (v == "1" || v == "true" || v == "TRUE");
        }();
        const char* reset = colorEnabled ? "\033[0m" : "";
        const char* labelColor = "";
        if (colorEnabled) {
            labelColor = (::strncmp(level, "ERROR", 5) == 0) ? "\033[38;5;88m" /* burgundy */ : "\033[35m" /* purple */;
        }
        if (*labelColor) {
            oss << "[" << labelColor << level << reset << "] " << file << ":" << line << ": " << msg << std::endl;
        } else {
            oss << "[" << level << "] " << file << ":" << line << ": " << msg << std::endl;
        }
        
        std::string logMessage = oss.str();
        
        // Always write to console: stderr when MCP_STDIO_MODE=1 to avoid corrupting stdout JSON-RPC frames
        static bool useStderr = [](){
            const std::string v = GetEnvOrDefault("MCP_STDIO_MODE", "0");
            return (v == "1" || v == "true" || v == "TRUE");
        }();
        if (useStderr) {
            std::cerr << logMessage;
        } else {
            std::cout << logMessage;
        }
        
        // Write to file if configured
        if (sLogFile.is_open()) {
            sLogFile << logMessage;
            sLogFile.flush();
        }
    }

    static LogLevel sLogLevel;

private:
    static std::ofstream sLogFile;
    static std::mutex sLogMutex;
};

// Static members declared but not defined here
// Definitions are in Logger.cpp

// Enhanced logging macros with log level filtering
#define LOG_DEBUG(fmt, ...) if (Logger::sLogLevel <= LogLevel::LOG_DEBUG_LEVEL) Logger::logf("DEBUG", fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  if (Logger::sLogLevel <= LogLevel::LOG_INFO_LEVEL)  Logger::logf("INFO", fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  if (Logger::sLogLevel <= LogLevel::LOG_WARN_LEVEL)  Logger::logf("WARN", fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) if (Logger::sLogLevel <= LogLevel::LOG_ERROR_LEVEL) Logger::logf("ERROR", fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) do { Logger::logf("FATAL", fmt, __FILE__, __LINE__, ##__VA_ARGS__); ::_Exit(EXIT_FAILURE); } while(0)

// HRESULT helper with categorized errors (Windows only)
#ifdef _WIN32
class HResult {
public:
    // Error categories for better organization
    enum class ErrorCategory {
        COM_ERROR,
        WINDOWS_API_ERROR,
        APP_ERROR,
        UNKNOWN_ERROR
    };

    static bool Check(HRESULT hr, const char* msg, const char* file, unsigned int line) {
    if (FAILED(hr)) {
        std::string winMsg = WStringToUtf8(Logger::GetErrorMessage(hr));
        std::string oss = std::format("{} (HRESULT=0x{:x})", msg, hr);
        if (!winMsg.empty()) {
            oss += " | WinErr: " + winMsg;
        }

        // Categorize the error
        ErrorCategory category = CategorizeError(hr);
        std::string categoryStr;
        switch (category) {
            case ErrorCategory::COM_ERROR:
                categoryStr = "COM_ERROR";
                break;
            case ErrorCategory::WINDOWS_API_ERROR:
                categoryStr = "WINDOWS_API_ERROR";
                break;
            case ErrorCategory::APP_ERROR:
                categoryStr = "APP_ERROR";
                break;
            default:
                categoryStr = "UNKNOWN_ERROR";
        }
        
        Logger::log(("HRESULT_FAIL:" + categoryStr).c_str(), oss, file, line);
        return false;
    }
    return true;
}

private:
    static ErrorCategory CategorizeError(HRESULT hr) {
        // E_FAIL, E_POINTER, etc. are COM errors
        if ((hr & 0x80000000) && (hr & 0x7FF) >= 0x00 && (hr & 0x7FF) <= 0x1FF) {
            return ErrorCategory::COM_ERROR;
        }
        
        // Windows API errors
        if ((hr & 0xC0070000) == 0xC0070000) {
            return ErrorCategory::WINDOWS_API_ERROR;
        }
        
        // Application-defined errors
        if ((hr & 0xA0000000) == 0xA0000000) {
            return ErrorCategory::APP_ERROR;
        }
        
        return ErrorCategory::UNKNOWN_ERROR;
    }
};

#define CHECK_HR(hr, msg) HResult::Check(hr, msg, __FILE__, __LINE__)
#else
// On non-Windows platforms, CHECK_HR is a no-op helper returning true.
#define CHECK_HR(hr, msg) (true)
#endif

// Function entry/exit macros for logging
#ifdef _DEBUG
#define FUNC_ENTRY() LOG_DEBUG("ENTER: {}", __FUNCTION__)
#define FUNC_EXIT()  LOG_DEBUG("EXIT:  {}", __FUNCTION__)

// Scope-based entry/exit guard to avoid manual pairs and ensure correct function on exit
namespace {
struct FuncScopeGuard {
    const char* func;
    explicit FuncScopeGuard(const char* f) : func(f) { LOG_DEBUG("ENTER: {}", func); }
    ~FuncScopeGuard() { LOG_DEBUG("EXIT:  {}", func); }
};
}
#define FUNC_SCOPE() [[maybe_unused]] FuncScopeGuard funcScope(__FUNCTION__)
#else
#define FUNC_ENTRY() ((void)0)
#define FUNC_EXIT()  ((void)0)
#define FUNC_SCOPE() ((void)0)
#endif
