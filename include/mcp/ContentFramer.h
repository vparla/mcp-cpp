//========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: ContentFramer.h
// Purpose: Interface for message framing (MCP stdio framing)
//========================================================================================================

#pragma once

#include <optional>
#include <string>
#include <memory>

namespace mcp {

class IContentFramer {
public:
    virtual ~IContentFramer() = default;
    enum class DecodeStatus {
        Ok,
        Incomplete,
        InvalidHeader,
        BodyTooLarge
    };
    struct DecodeResult {
        DecodeStatus status;
        std::optional<std::string> payload; // present when status==Ok
        std::size_t bytesConsumed{0};       // header+sep or full frame bytes to drop when appropriate
    };
    virtual std::string encode(const std::string& payload) = 0;
    virtual std::optional<std::string> tryDecode(std::string& buffer) = 0;
    virtual DecodeResult tryDecodeEx(const std::string& buffer) = 0;
};

std::unique_ptr<IContentFramer> MakeContentLengthFramer(std::size_t maxContentLength = 1024 * 1024);

} // namespace mcp
