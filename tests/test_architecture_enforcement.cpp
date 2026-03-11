//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_architecture_enforcement.cpp
// Purpose: Architecture enforcement tests for layering, shared-core placement, and validator paths
//==========================================================================================================

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::string out;
    in.seekg(0, std::ios::end);
    out.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    return out;
}

std::vector<fs::path> CollectFiles(const fs::path& root, const std::unordered_set<std::string>& exts) {
    std::vector<fs::path> files;
    if (!fs::exists(root)) {
        return files;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (exts.find(ext) != exts.end()) {
            files.push_back(entry.path());
        }
    }
    return files;
}

std::vector<std::string> QuotedIncludes(const std::string& fileText) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        pos = fileText.find("#include \"", pos);
        if (pos == std::string::npos) {
            break;
        }
        pos += std::string("#include \"").size();
        const size_t end = fileText.find('"', pos);
        if (end == std::string::npos) {
            break;
        }
        out.push_back(fileText.substr(pos, end - pos));
        pos = end + 1;
    }
    return out;
}

void ExpectContainsAll(const std::string& text,
                       const std::vector<std::string>& needles,
                       const std::string& contextLabel) {
    for (const auto& needle : needles) {
        EXPECT_NE(text.find(needle), std::string::npos)
            << contextLabel << " missing required token: " << needle;
    }
}

}  // namespace

TEST(ArchitectureLayers, PublicHeadersDoNotIncludePrivateOrTestPaths) {
    const fs::path repoRoot = fs::path(MCP_REPO_ROOT);
    const auto headers = CollectFiles(repoRoot / "include" / "mcp", {".h", ".hpp"});
    ASSERT_FALSE(headers.empty()) << "No public headers were discovered under include/mcp";

    for (const auto& header : headers) {
        const auto text = ReadFile(header);
        ASSERT_FALSE(text.empty()) << "Failed to read " << header.string();
        const auto includes = QuotedIncludes(text);
        for (const auto& inc : includes) {
            EXPECT_EQ(inc.find("src/"), std::string::npos)
                << header.string() << " includes private source path: " << inc;
            EXPECT_EQ(inc.find("tests/"), std::string::npos)
                << header.string() << " includes test path: " << inc;
            EXPECT_EQ(inc.find("examples/"), std::string::npos)
                << header.string() << " includes example path: " << inc;
        }
    }
}

TEST(ArchitectureLayers, SourceFilesDoNotIncludeTestsOrExamples) {
    const fs::path repoRoot = fs::path(MCP_REPO_ROOT);
    const auto sources = CollectFiles(repoRoot / "src", {".cpp", ".cc", ".cxx"});
    ASSERT_FALSE(sources.empty()) << "No source files were discovered under src";

    for (const auto& source : sources) {
        const auto text = ReadFile(source);
        ASSERT_FALSE(text.empty()) << "Failed to read " << source.string();
        const auto includes = QuotedIncludes(text);
        for (const auto& inc : includes) {
            EXPECT_EQ(inc.find("tests/"), std::string::npos)
                << source.string() << " includes test path: " << inc;
            EXPECT_EQ(inc.find("examples/"), std::string::npos)
                << source.string() << " includes example path: " << inc;
        }
    }
}

TEST(ArchitectureShared, CanonicalCoreImplementationsAreUnique) {
    const fs::path repoRoot = fs::path(MCP_REPO_ROOT);
    const auto sources = CollectFiles(repoRoot / "src", {".cpp"});
    ASSERT_FALSE(sources.empty()) << "No source files were discovered under src";

    std::unordered_map<std::string, int> baseNameCounts;
    for (const auto& source : sources) {
        baseNameCounts[source.filename().string()]++;
    }

    const std::vector<std::string> canonicalFiles = {
        "JsonRpcMessageRouter.cpp",
        "ContentLengthFramer.cpp",
        "WwwAuthenticate.cpp",
        "ServerAuth.cpp",
        "OAuthDiscovery.cpp",
    };

    for (const auto& fileName : canonicalFiles) {
        const auto it = baseNameCounts.find(fileName);
        ASSERT_TRUE(it != baseNameCounts.end()) << "Missing canonical implementation file: " << fileName;
        EXPECT_EQ(it->second, 1) << "Duplicate canonical implementation file detected: " << fileName;
    }
}

TEST(ArchitectureValidation, ServerPathsUseCanonicalValidatorsAndMethodConstants) {
    const fs::path serverPath = fs::path(MCP_REPO_ROOT) / "src" / "mcp" / "Server.cpp";
    const auto text = ReadFile(serverPath);
    ASSERT_FALSE(text.empty()) << "Failed to read " << serverPath.string();

    ExpectContainsAll(
        text,
        {
            "validation::validateToolsListResultJson",
            "validation::validateResourcesListResultJson",
            "validation::validateResourceTemplatesListResultJson",
            "validation::validatePromptsListResultJson",
            "validation::validateRootsListResultJson",
            "validation::validateCallToolResultJson",
            "validation::validateReadResourceResultJson",
            "validation::validateGetPromptResult",
            "validation::validateCompletionResultJson",
            "validation::validateCreateMessageParamsJson",
            "validation::validateCreateMessageResultJson",
            "validation::validateElicitationRequestJson",
            "validation::validateElicitationResultJson",
            "validation::validatePingResultJson",
        },
        "Server.cpp");

    ExpectContainsAll(
        text,
        {
            "req.method == Methods::Initialize",
            "req.method == Methods::Ping",
            "req.method == Methods::ListTools",
            "req.method == Methods::CallTool",
            "req.method == Methods::Complete",
            "req.method == Methods::ListResources",
            "req.method == Methods::ReadResource",
            "req.method == Methods::ListPrompts",
            "req.method == Methods::GetPrompt",
        },
        "Server.cpp dispatch");

    ExpectContainsAll(
        text,
        {
            "notification->method == Methods::RootListChanged",
        },
        "Server.cpp notifications");

    EXPECT_EQ(text.find("req.method == \""), std::string::npos)
        << "Server request dispatch should use Methods constants, not raw string literals.";
}

TEST(ArchitectureValidation, ClientStrictPathsUseCanonicalValidators) {
    const fs::path clientPath = fs::path(MCP_REPO_ROOT) / "src" / "mcp" / "Client.cpp";
    const auto text = ReadFile(clientPath);
    ASSERT_FALSE(text.empty()) << "Failed to read " << clientPath.string();

    ExpectContainsAll(
        text,
        {
            "this->validationMode == validation::ValidationMode::Strict",
            "validation::validateCreateMessageParamsJson",
            "validation::validateCreateMessageResultJson",
            "validation::validateCompletionResultJson",
            "validation::validateElicitationRequestJson",
            "validation::validateElicitationResultJson",
            "validation::validatePingResultJson",
            "validation::validateToolsListResultJson",
            "validation::validateResourcesListResultJson",
            "validation::validateResourceTemplatesListResultJson",
            "validation::validatePromptsListResultJson",
            "validation::validateRootsListResultJson",
            "req.method == Methods::Ping",
            "req.method == Methods::Elicit",
            "req.method == Methods::ListRoots",
        },
        "Client.cpp");
}
