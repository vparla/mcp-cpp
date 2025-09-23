//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_resource_read_chunking.cpp
// Purpose: Experimental resource read chunking (offset/length) tests
//==========================================================================================================

#include <gtest/gtest.h>
#include <future>
#include <chrono>
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
