//========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: ContentLengthFramer.cpp
// Purpose: Default Content-Length based framer for MCP stdio transport
//========================================================================================================

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>

#include "logging/Logger.h"
#include "mcp/ContentFramer.h"

namespace mcp {

namespace {
class ContentLengthFramer : public IContentFramer {
public:
    explicit ContentLengthFramer(std::size_t maxLen) : maxContentLength(maxLen) {}

    std::string encode(const std::string& payload) override {
        std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        std::string frame; frame.reserve(header.size() + payload.size());
        frame.append(header);
        frame.append(payload);
        return frame;
    }

    DecodeResult tryDecodeEx(const std::string& buffer) override {
        const std::string sep = "\r\n\r\n";
        std::size_t headerEnd = buffer.find(sep);
        if (headerEnd == std::string::npos) {
            return { DecodeStatus::Incomplete, std::nullopt, 0 };
        }

        std::size_t pos = 0;
        std::size_t contentLength = 0;
        bool haveLength = false;
        while (pos < headerEnd) {
            std::size_t eol = buffer.find("\r\n", pos);
            if (eol == std::string::npos || eol > headerEnd) {
                break;
            }
            std::string line = buffer.substr(pos, eol - pos);
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                std::string value = line.substr(colon + 1);
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch){ return !std::isspace(ch); }));
                if (name == "content-length") {
                    try {
                        unsigned long long v64 = std::stoull(value);
                        if (v64 > maxContentLength || v64 > std::numeric_limits<std::size_t>::max()) {
                            LOG_WARN("Content-Length {} exceeds limits (max={})", v64, maxContentLength);
                            return { DecodeStatus::BodyTooLarge, std::nullopt, headerEnd + sep.size() };
                        }
                        contentLength = static_cast<std::size_t>(v64);
                        haveLength = true;
                    } catch (...) {
                        LOG_WARN("Invalid Content-Length header: {}", value);
                        return { DecodeStatus::InvalidHeader, std::nullopt, headerEnd + sep.size() };
                    }
                }
            }
            pos = eol + 2;
        }

        if (!haveLength) {
            LOG_WARN("Missing Content-Length header");
            return { DecodeStatus::InvalidHeader, std::nullopt, headerEnd + sep.size() };
        }

        const std::size_t headerAndSep = headerEnd + sep.size();
        if (contentLength > std::numeric_limits<std::size_t>::max() - headerAndSep) {
            LOG_WARN("Frame size overflow detected (header={}, len={})", headerAndSep, contentLength);
            return { DecodeStatus::InvalidHeader, std::nullopt, headerEnd + sep.size() };
        }
        std::size_t frameTotal = headerAndSep + contentLength;
        if (buffer.size() < frameTotal) {
            return { DecodeStatus::Incomplete, std::nullopt, 0 };
        }

        std::string payload = buffer.substr(headerAndSep, contentLength);
        return { DecodeStatus::Ok, std::make_optional(std::move(payload)), frameTotal };
    }

    std::optional<std::string> tryDecode(std::string& buffer) override {
        DecodeResult r = tryDecodeEx(buffer);
        if (r.status == DecodeStatus::Ok && r.payload.has_value()) {
            if (r.bytesConsumed > 0 && r.bytesConsumed <= buffer.size()) {
                buffer.erase(0, r.bytesConsumed);
            }
            return r.payload;
        }
        return std::nullopt;
    }

private:
    std::size_t maxContentLength;
};
} // namespace

std::unique_ptr<IContentFramer> MakeContentLengthFramer(std::size_t maxContentLength) {
    return std::make_unique<ContentLengthFramer>(maxContentLength);
}

} // namespace mcp
