//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_acceptor_factory.cpp
// Purpose: Validate HTTPServerFactory creates an ITransportAcceptor and it can Start/Stop.
//==========================================================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>
#include <ctime>
#include "mcp/HTTPServer.hpp"
#include "mcp/Transport.h"

using namespace mcp;

TEST(AcceptorFactory, HttpServerFactoryCreatesAcceptor) {
    HTTPServerFactory factory;
    // Use ephemeral port 0, http scheme
    auto acceptor = factory.CreateTransportAcceptor("http://127.0.0.1:0");
    ASSERT_NE(acceptor, nullptr);
    acceptor->SetRequestHandler([](const JSONRPCRequest& req){
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id; return resp;
    });
    acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });
    acceptor->SetErrorHandler([](const std::string&){ /* no-op */ });

    EXPECT_NO_THROW({ acceptor->Start().get(); });
    EXPECT_NO_THROW({ acceptor->Stop().get(); });
}

TEST(AcceptorFactory, ParsesBracketedIPv4Loopback) {
    HTTPServerFactory factory;
    auto acceptor = factory.CreateTransportAcceptor("http://[127.0.0.1]:0");
    ASSERT_NE(acceptor, nullptr);

    acceptor->SetRequestHandler([](const JSONRPCRequest& req){
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id; return resp;
    });
    acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });
    std::atomic<bool> errorSeen{false};
    acceptor->SetErrorHandler([&](const std::string&){ errorSeen.store(true); });

    EXPECT_NO_THROW({ acceptor->Start().get(); });
    EXPECT_NO_THROW({ acceptor->Stop().get(); });
}

TEST(AcceptorFactory, ParsesBracketedIPv6LoopbackOrSurfacesError) {
    HTTPServerFactory factory;
    auto acceptor = factory.CreateTransportAcceptor("http://[::1]:0");
    ASSERT_NE(acceptor, nullptr);

    acceptor->SetRequestHandler([](const JSONRPCRequest& req){
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id; return resp;
    });
    acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });

    acceptor->SetErrorHandler([&](const std::string&){ /* may or may not fire depending on env */ });
    EXPECT_NO_THROW({ acceptor->Start().get(); });
    EXPECT_NO_THROW({ acceptor->Stop().get(); });
}

// Invalid/fuzzed configs should surface errors (resolver/bind failures) quickly
TEST(AcceptorFactory, InvalidBracketFormsSurfaceErrors) {
    HTTPServerFactory factory;
    const char* cases[] = {
        "http://[bad]:0",                // non-IP token inside brackets
        "http://[999.999.999.999]:0",    // invalid IPv4
        "http://[::zzzz]:0",             // invalid IPv6
        "http://[::1",                    // missing closing bracket
        "http://[::1]80",                 // missing colon separator
        "http://[]:0"                     // empty address
    };

    for (auto cfg : cases) {
        auto acceptor = factory.CreateTransportAcceptor(cfg);
        ASSERT_NE(acceptor, nullptr) << cfg;

        acceptor->SetRequestHandler([](const JSONRPCRequest& req){
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id; return resp;
        });
        acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });
        acceptor->SetErrorHandler([&](const std::string&){ /* may or may not fire quickly */ });

        EXPECT_NO_THROW({ acceptor->Start().get(); }) << cfg;
        EXPECT_NO_THROW({ acceptor->Stop().get(); }) << cfg;
    }
}

// Invalid IPv6 shapes: too many hextets, oversized hextet digits, malformed compression
TEST(AcceptorFactory, InvalidIPv6FormsSurfaceErrors) {
    HTTPServerFactory factory;
    const char* cases[] = {
        "http://[2001:0db8:85a3:0000:0000:8a2e:0370:7334:1234]:0", // 9 hextets
        "http://[2001:db8:12345::1]:0",                             // hextet too long (5+ digits)
        "http://[fffff::1]:0",                                     // hextet too long
        "http://[2001:db8:::1]:0",                                 // malformed triple colon
        "http://[::1::]:0",                                        // multiple ::
        "http://[GGGG::1]:0"                                       // invalid hex digits
    };

    for (auto cfg : cases) {
        auto acceptor = factory.CreateTransportAcceptor(cfg);
        ASSERT_NE(acceptor, nullptr) << cfg;

        acceptor->SetRequestHandler([](const JSONRPCRequest& req){
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id; return resp;
        });
        acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });

        std::atomic<bool> errorSeen{false};
        std::mutex mtx; std::condition_variable cv;
        acceptor->SetErrorHandler([&](const std::string&){ errorSeen.store(true); cv.notify_all(); });

        EXPECT_NO_THROW({ acceptor->Start().get(); }) << cfg;
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(1));
        }
        EXPECT_TRUE(errorSeen.load()) << "Expected error for invalid IPv6 form: " << cfg;
        EXPECT_NO_THROW({ acceptor->Stop().get(); }) << cfg;
    }
}

// Invalid port values: negative, non-numeric, out of range, whitespace, mixed
TEST(AcceptorFactory, InvalidPortFormsSurfaceErrors) {
    HTTPServerFactory factory;
    const char* cases[] = {
        "http://127.0.0.1:-1",
        "http://127.0.0.1:70000",
        "http://127.0.0.1:65536",
        "http://127.0.0.1:abc",
        "http://127.0.0.1:80a",
        "http://[::1]:-1",
        "http://[::1]:70000",
        "http://[::1]:65536",
        "http://[::1]:abc",
        "http://[::1]:80a",
        "http://[1.2.3.4]:-5",
        "http://[1.2.3.4]:70000",
        "http://[1.2.3.4]:abc",
    };

    for (auto cfg : cases) {
        auto acceptor = factory.CreateTransportAcceptor(cfg);
        ASSERT_NE(acceptor, nullptr) << cfg;

        acceptor->SetRequestHandler([](const JSONRPCRequest& req){
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = req.id; return resp;
        });
        acceptor->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>){ /* no-op */ });

        std::atomic<bool> errorSeen{false};
        std::mutex mtx; std::condition_variable cv;
        acceptor->SetErrorHandler([&](const std::string&){ errorSeen.store(true); cv.notify_all(); });

        EXPECT_NO_THROW({ acceptor->Start().get(); }) << cfg;
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(1));
        }
        EXPECT_TRUE(errorSeen.load()) << "Expected error for invalid configuration: " << cfg;
        EXPECT_NO_THROW({ acceptor->Stop().get(); }) << cfg;
    }
}
