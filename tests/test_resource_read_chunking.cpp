//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_read_chunking.cpp
// Purpose: Experimental resource read chunking (offset/length) tests
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>
#include <optional>
#include <string>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

namespace {

static ReadResourceResult makeTextResource(const std::string& s) {
    ReadResourceResult r; r.contents.push_back(mcp::typed::makeText(s)); return r;
}

TEST(ResourceReadChunking, ClampEnforcedRange) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJ"; // 10 bytes

    // Reduce server clamp to 4 bytes
    {
        ServerCapabilities caps = server.GetCapabilities();
        JSONValue::Object rrc;
        rrc["enabled"] = std::make_shared<JSONValue>(true);
        rrc["maxChunkBytes"] = std::make_shared<JSONValue>(static_cast<int64_t>(4));
        caps.experimental["resourceReadChunking"] = JSONValue{rrc};
        server.SetCapabilities(caps);
    }

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities ccaps; (void)client->Initialize(ci, ccaps).get();

    // Ask for more than clamp; expect exactly 4 bytes slice starting at offset 2 => "CDEF"
    auto r = mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(2), std::optional<int64_t>(10)).get();
    auto c = mcp::typed::collectText(r);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], std::string("CDEF"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, ReadAllResourceInChunks_ClampSmallerThanRequested) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJ"; // 10 bytes

    // Reduce server clamp to 3 bytes; client will request 8 but only receive 3 per call
    {
        ServerCapabilities caps = server.GetCapabilities();
        JSONValue::Object rrc;
        rrc["enabled"] = std::make_shared<JSONValue>(true);
        rrc["maxChunkBytes"] = std::make_shared<JSONValue>(static_cast<int64_t>(3));
        caps.experimental["resourceReadChunking"] = JSONValue{rrc};
        server.SetCapabilities(caps);
    }

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities ccaps; (void)client->Initialize(ci, ccaps).get();

    const size_t requestedChunk = 8;
    auto agg = mcp::typed::readAllResourceInChunks(*client, uri, requestedChunk).get();
    std::string reassembled;
    for (const auto& s : mcp::typed::collectText(agg)) reassembled += s;
    EXPECT_EQ(reassembled, text);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, RangeAcrossMultipleTextChunks) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://multi";

    // Return two text chunks: "ABC" + "DEF" => flattened "ABCDEF"
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){
            ReadResourceResult r;
            r.contents.push_back(mcp::typed::makeText("ABC"));
            r.contents.push_back(mcp::typed::makeText("DEF"));
            return r;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Offset=4, length=2 over "ABCDEF" -> "EF"
    auto rr = mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(4), std::optional<int64_t>(2)).get();
    auto parts = mcp::typed::collectText(rr);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], std::string("EF"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, MixedContent_WithRange_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://mixed";

    // Return one text and one non-text item; range read should fail
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){
            ReadResourceResult r;
            r.contents.push_back(mcp::typed::makeText("hello"));
            JSONValue::Object obj; obj["data"] = std::make_shared<JSONValue>(std::string("0101"));
            r.contents.push_back(JSONValue{obj});
            return r;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(0), std::optional<int64_t>(4)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
} // namespace

TEST(ResourceReadChunking, RangeBasicSlices) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    server.RegisterResource(uri, [=](const std::string& reqUri, std::stop_token){
        return std::async(std::launch::async, [=](){
            EXPECT_EQ(reqUri, uri);
            return makeTextResource(text);
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // offset=0, length=5 => ABCDE
    auto r1 = mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(0), static_cast<int64_t>(5)).get();
    auto c1 = mcp::typed::collectText(r1);
    ASSERT_EQ(c1.size(), 1u);
    EXPECT_EQ(c1[0], std::string("ABCDE"));

    // offset=5, length=5 => FGHIJ
    auto r2 = mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(5), static_cast<int64_t>(5)).get();
    auto c2 = mcp::typed::collectText(r2);
    ASSERT_EQ(c2.size(), 1u);
    EXPECT_EQ(c2[0], std::string("FGHIJ"));

    // offset beyond => empty
    auto r3 = mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(9999), static_cast<int64_t>(5)).get();
    EXPECT_TRUE(r3.contents.empty());

    // offset with no length => to end
    auto r4 = mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(3), std::nullopt).get();
    auto c4 = mcp::typed::collectText(r4);
    ASSERT_EQ(c4.size(), 1u);
    EXPECT_EQ(c4[0], text.substr(3));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, RangeOnlyLength_SlicesFromStart) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Only length provided: expect slice from start
    auto r = mcp::typed::readResourceRange(*client, uri, std::nullopt, std::optional<int64_t>(5)).get();
    auto c = mcp::typed::collectText(r);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], std::string("ABCDE"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, RangeLargeLength_ClampedToEnd) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Length exceeds remaining; expect clamped to end
    auto r = mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(24), std::optional<int64_t>(100)).get();
    auto c = mcp::typed::collectText(r);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], std::string("YZ"));

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, InvalidParams_NegativeLength) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "hello";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Negative length -> InvalidParams -> wrapper throws
    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(0), std::optional<int64_t>(-5)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, InvalidParams_OnlyLengthZero) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "hello";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Only length=0 provided -> InvalidParams -> wrapper throws
    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, std::nullopt, std::optional<int64_t>(0)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, NonTextTypeField_WithRange_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://bin2";

    // Return content with type != "text"; range read should fail
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){
            ReadResourceResult r;
            JSONValue::Object obj; obj["type"] = std::make_shared<JSONValue>(std::string("image")); obj["text"] = std::make_shared<JSONValue>(std::string("data"));
            r.contents.push_back(JSONValue{obj});
            return r;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(0), std::optional<int64_t>(4)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, TextTypeMissingTextField_WithRange_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://weird";

    // Return type="text" but no text field; range read should fail
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){
            ReadResourceResult r;
            JSONValue::Object obj; obj["type"] = std::make_shared<JSONValue>(std::string("text"));
            r.contents.push_back(JSONValue{obj});
            return r;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, std::optional<int64_t>(0), std::optional<int64_t>(4)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, InvalidParams_NegativeOffsetOrZeroLength) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "hello world";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // Negative offset -> InvalidParams -> wrapper throws
    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(-1), static_cast<int64_t>(5)).get(), std::runtime_error);

    // Zero length -> InvalidParams -> wrapper throws
    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(0), static_cast<int64_t>(0)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, NonTextContent_WithRange_ReturnsError) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://bin";

    // Return non-text content; range read should fail with InternalError
    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [](){
            ReadResourceResult r;
            JSONValue::Object obj; obj["data"] = std::make_shared<JSONValue>(std::string("0101"));
            r.contents.push_back(JSONValue{obj});
            return r;
        });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    EXPECT_THROW((void)mcp::typed::readResourceRange(*client, uri, static_cast<int64_t>(0), static_cast<int64_t>(4)).get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, ReadAllResourceInChunks_Reassembles) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = std::string(10, 'A') + std::string(10, 'B') + std::string(5, 'C'); // 25 bytes

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    const size_t chunkSize = 8;
    auto agg = mcp::typed::readAllResourceInChunks(*client, uri, chunkSize).get();
    std::string reassembled;
    for (const auto& s : mcp::typed::collectText(agg)) reassembled += s;
    EXPECT_EQ(reassembled, text);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(ResourceReadChunking, ReadAllResourceInChunks_ZeroChunkSize_Throws) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    Server server("Srv");
    const std::string uri = "mem://doc";
    const std::string text = "hello world";

    server.RegisterResource(uri, [=](const std::string&, std::stop_token){
        return std::async(std::launch::async, [=](){ return makeTextResource(text); });
    });

    ASSERT_NO_THROW(server.Start(std::move(serverTrans)).get());

    ClientFactory f; Implementation ci{"Cli","1.0"};
    auto client = f.CreateClient(ci);
    ASSERT_NO_THROW(client->Connect(std::move(clientTrans)).get());
    ClientCapabilities caps; (void)client->Initialize(ci, caps).get();

    // chunkSize=0 must throw invalid_argument from typed helper
    EXPECT_THROW((void)mcp::typed::readAllResourceInChunks(*client, uri, 0).get(), std::invalid_argument);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}
