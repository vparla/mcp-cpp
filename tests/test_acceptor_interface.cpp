//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_acceptor_interface.cpp
// Purpose: Basic interface compile and minimal behavior checks for ITransportAcceptor integration
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/HTTPServer.hpp"
#include "mcp/Transport.h"

using namespace mcp;

TEST(AcceptorInterface, HttpServerImplementsAcceptor) {
    HTTPServer::Options opts; opts.scheme = "http"; opts.address = "127.0.0.1"; opts.port = "0"; // ephemeral
    HTTPServer server(opts);

    // Handler registration compiles and stores
    bool requestSeen = false;
    bool notifySeen = false;
    bool errorSeen = false;

    server.SetRequestHandler([&](const JSONRPCRequest& req){
        requestSeen = true;
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = req.id;
        return resp;
    });
    server.SetNotificationHandler([&](std::unique_ptr<JSONRPCNotification> note){
        (void)note; notifySeen = true; /* can't inject traffic here */
    });
    server.SetErrorHandler([&](const std::string& err){
        (void)err; errorSeen = true; /* expect no error in Start/Stop */
    });

    // Start and Stop should work (no incoming traffic)
    ASSERT_NO_THROW({ server.Start().get(); });
    ASSERT_NO_THROW({ server.Stop().get(); });

    // Sanity: flags untouched (no traffic), but registration happened
    EXPECT_FALSE(requestSeen);
    EXPECT_FALSE(notifySeen);
    // errorSeen may be false (no error expected)
}
