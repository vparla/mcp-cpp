//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: test_validation_mode.cpp
// Purpose: Validation mode (Off/Strict) scaffolding tests for client and server
//==========================================================================================================

#include <gtest/gtest.h>
#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

TEST(ValidationMode, ServerToggle) {
    Server server("Validation Test Server");
    // Default should be Off
    EXPECT_EQ(server.GetValidationMode(), mcp::validation::ValidationMode::Off);
    // Toggle to Strict
    server.SetValidationMode(mcp::validation::ValidationMode::Strict);
    EXPECT_EQ(server.GetValidationMode(), mcp::validation::ValidationMode::Strict);
}

TEST(ValidationMode, ClientToggle) {
    Implementation clientInfo{"Validation Test Client", "1.0.0"};
    ClientFactory f;
    auto client = f.CreateClient(clientInfo);
    // Default should be Off
    EXPECT_EQ(client->GetValidationMode(), mcp::validation::ValidationMode::Off);
    // Toggle to Strict
    client->SetValidationMode(mcp::validation::ValidationMode::Strict);
    EXPECT_EQ(client->GetValidationMode(), mcp::validation::ValidationMode::Strict);
}
