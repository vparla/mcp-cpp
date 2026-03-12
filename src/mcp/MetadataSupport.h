//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/MetadataSupport.h
// Purpose: Shared helpers for MCP metadata serialization and parsing
//==========================================================================================================

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mcp/Protocol.h"

namespace mcp::metadata {

inline bool IsSupportedIconTheme(const std::string& theme) {
    return theme == "light" || theme == "dark";
}

inline JSONValue::Object SerializeIconObject(const Icon& icon) {
    JSONValue::Object object;
    object["src"] = std::make_shared<JSONValue>(icon.src);
    if (icon.mimeType.has_value()) {
        object["mimeType"] = std::make_shared<JSONValue>(icon.mimeType.value());
    }
    if (icon.sizes.has_value()) {
        JSONValue::Array sizesArray;
        for (const auto& size : icon.sizes.value()) {
            sizesArray.push_back(std::make_shared<JSONValue>(size));
        }
        object["sizes"] = std::make_shared<JSONValue>(sizesArray);
    }
    if (icon.theme.has_value()) {
        object["theme"] = std::make_shared<JSONValue>(icon.theme.value());
    }
    return object;
}

inline JSONValue SerializeIcons(const std::vector<Icon>& icons) {
    JSONValue::Array iconsArray;
    for (const auto& icon : icons) {
        iconsArray.push_back(std::make_shared<JSONValue>(SerializeIconObject(icon)));
    }
    return JSONValue{iconsArray};
}

inline void AddOptionalIconsField(JSONValue::Object& object,
                                  const std::optional<std::vector<Icon>>& icons) {
    if (!icons.has_value()) {
        return;
    }
    object["icons"] = std::make_shared<JSONValue>(SerializeIcons(icons.value()));
}

inline std::optional<Icon> ParseIconObject(const JSONValue::Object& object) {
    auto srcIt = object.find("src");
    if (srcIt == object.end() || !srcIt->second ||
        !std::holds_alternative<std::string>(srcIt->second->value)) {
        return std::nullopt;
    }

    Icon icon;
    icon.src = std::get<std::string>(srcIt->second->value);

    auto mimeTypeIt = object.find("mimeType");
    if (mimeTypeIt != object.end()) {
        if (!mimeTypeIt->second ||
            !std::holds_alternative<std::string>(mimeTypeIt->second->value)) {
            return std::nullopt;
        }
        icon.mimeType = std::get<std::string>(mimeTypeIt->second->value);
    }

    auto sizesIt = object.find("sizes");
    if (sizesIt != object.end()) {
        if (!sizesIt->second ||
            !std::holds_alternative<JSONValue::Array>(sizesIt->second->value)) {
            return std::nullopt;
        }
        std::vector<std::string> sizes;
        const auto& sizesArray = std::get<JSONValue::Array>(sizesIt->second->value);
        sizes.reserve(sizesArray.size());
        for (const auto& sizeValue : sizesArray) {
            if (!sizeValue || !std::holds_alternative<std::string>(sizeValue->value)) {
                return std::nullopt;
            }
            sizes.push_back(std::get<std::string>(sizeValue->value));
        }
        icon.sizes = std::move(sizes);
    }

    auto themeIt = object.find("theme");
    if (themeIt != object.end()) {
        if (!themeIt->second ||
            !std::holds_alternative<std::string>(themeIt->second->value)) {
            return std::nullopt;
        }
        const auto& theme = std::get<std::string>(themeIt->second->value);
        if (!IsSupportedIconTheme(theme)) {
            return std::nullopt;
        }
        icon.theme = theme;
    }

    return icon;
}

inline std::optional<std::vector<Icon>> ParseIconsField(const JSONValue::Object& object,
                                                        const char* key) {
    auto iconsIt = object.find(key);
    if (iconsIt == object.end()) {
        return std::nullopt;
    }
    if (!iconsIt->second ||
        !std::holds_alternative<JSONValue::Array>(iconsIt->second->value)) {
        return std::nullopt;
    }

    std::vector<Icon> icons;
    const auto& iconsArray = std::get<JSONValue::Array>(iconsIt->second->value);
    icons.reserve(iconsArray.size());
    for (const auto& iconValue : iconsArray) {
        if (!iconValue ||
            !std::holds_alternative<JSONValue::Object>(iconValue->value)) {
            return std::nullopt;
        }
        auto icon = ParseIconObject(std::get<JSONValue::Object>(iconValue->value));
        if (!icon.has_value()) {
            return std::nullopt;
        }
        icons.push_back(std::move(icon.value()));
    }
    return icons;
}

}  // namespace mcp::metadata
