

# MCP C++ SDK Implementation

* In the role of a CPP 20 expert, create a new C++ MCP SDK implementation in mcp-cpp directory with the following features:

* Using go-sdk and mcp-go as recipes, create a new CPP iMCP SDK mplementation in mcp-cpp.  Follow the standard defined here: https://modelcontextprotocol.io/specification/, using the latest revision. You can also reference SDKs at https://github.com/modelcontextprotocol for additional insights.

* Review the C:\Work\go-sdk\design\design.md file for a high level overview of the go-sdk implementation.

* DO NOT COPY GO VERBATIM,  Instead, use it as inspiration to create an expert level CPP SDK for the MCP protocol.

* Follow the existing directory/file structure in mcp-cpp as a template and create appropriate folders and files to implement the SDK.  


* Use good interface design and object oriented programming practices. Conceptually style interfaces similar to how a COM program would create an interface and implement it.  Ideally a cross-platform IDL-style solution where the IDL is translated to C++ headers as a compiler operation.

* Always use safe coding practices and best practices, including safe memory management, error handling, and security.

* Always use multiline and braces {} for control flow statements and functions as well as scoping operations. use syling guidelines similar to the following thoughout code for readability:
```cpp
if (condition) { 
    ... 
} else { 
    ... 
}

while (condition) { 
    ... 
}

do {
    ... 
} while (condition);

for (size_t i = 0; i < limit; ++i) { 
    ... 
}

void func(size_t param) {
    ... 
}

try { 
    t->Start().get(); 
} catch (...) { 
    LOG_ERROR("Start failed"); 
}
```

* CRITICAL ------ NEVER EVER do this one-liner style statements for any code.  Always use multiline and braces {} for control flow statements and functions as well as scoping operations as noted above, properly formatting the code for readability.
    ```cpp
    try { sslCtx->set_default_verify_paths(); } catch (...) {}
    if (!opts.caFile.empty()) { ::ERR_clear_error(); sslCtx->load_verify_file(opts.caFile) return; }
    if (!opts.caPath.empty()) { ::ERR_clear_error(); sslCtx->add_verify_path(opts.caPath); return; }

    if (!handler){ errors::McpError e; e.code = JSONRPCErrorCodes::ToolNotFound; e.message = "Tool not found"; return errors::makeErrorResponse(req.id, e); }
    ```

* Always place includes after copyright header and before any code at the top of the file. making sure to group includes by type and order them alphabetically and place < > includes before " " includes.

* Always use size_t for array sizes and loop counters and other size related operations.

* Always use unsigned int for counter operations.

* Use the latest version of the C++ standard (C++20 or later) and features of that standard as well as the latest version of the CMake build system.

* Use Advanced C++ capabilities whenever possible, leverating C++20 features.

* Polling / Blocking / Busy-loops / Sleeps must be avoided in favor of callbacks, events and async design patterns only.  Use futures and promises to implement async operations as well as C++20 coroutines.

* All code must compile with no errors or warnings and run on Windows, macOS, and Linux as 64bit native code.

* Do not introduce 3rd party libraries to the codebase.  Instead, implement a minimalistic and efficient solution using only the standard library and c-runtime code.  For example, you can create a minimalistic JSON parser using only the standard library or JSON-RPC using only the standard library.

* Unit tests can be implemented using the GoogleTest framework.

* Create complete negative-test cases for all code.

* Complete coding and do not add TODO tasks.  100% Finish the task and move on to the next task when approved to do so.

* Generate appropriate documentation for the SDK, including API documentation, installation instructions, and usage examples.

* Test the SDK on all supported platforms and ensure that it works as expected.

* Use the current logger, but make it thread safe and cross-platform.  Add logging to the SDK to log all SDK activity and other information to the console.  Add a logging level to the SDK to control the amount of logging.

* Do changes in very small increments and validate they build and function before proceeding to the next step after getting permission to proceed.

* Create a Docker container for building and testing the SDK for Windows, macOS, and Linux, using the same multi-stage Dockerfile approach as the hello world example.

* Create and maintain detailed view of the directory structure in tree format and explain what each file does.  Add this to the core readme about the SDK and keep it up to date as you add new files and capabilities.  

* Functions must not exceed 75 lines of code.  Break up functions into smaller sub-functions as needed, making them protected or private as appropriate.  Declare them inline in the implementation file when appropriate. use static declarations when appropriate.  Do not create C-style functions - prefer C++ style functions for this task of modularization.

* Make sure all files have the correct license header and copyright information similar to the following and always include all 4 parts as shown in example:
    // SPDX-License-Identifier: MIT
    // Copyright (c) 2025 Vinny Parla
    // File: Transport.cpp
    // Purpose: MCP transport layer implementations

* Other features:

    * Tests with GoogleTest
    * clang-format + clang-tidy
    * Docker-based reproducible build (multi-arch capable)
    * GitHub Actions CI for all 3 OSes
    * MIT license and a clear README
    * catch statements should not be empty and should at least DEBUG log the exception message for troubleshooting, unless it is a truly benign exception.


** Do small tasks that can be easily tested and verified and pause for feedback before moving on to the next task. **

** Indicate if there are incomplete tasks needing completion and create an appropriate memory of the task. **

** REFERENCES ** 
https://modelcontextprotocol.io/specification/2025-06-18

https://google.github.io/styleguide/cppguide.html#C++_Version <Style guide>


## COPYRIGHT HEADER FORMAT ##

Use # when appropriate in other file types.  ALL files must have a copyright header similar to the following:

//========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Transport.h
// Purpose: MCP transport layer interface definition. 
//========================================================================================================


## FUNCTION DOCUMENTATION FORMAT ##

//========================================================================================================
// Computes the factorial of a non-negative integer.
// Args:
//   n: The input integer. Must be non-negative.
// Returns:
//   The factorial of n, or 0 if n is negative (error case)
//========================================================================================================
int funct(int n);

//========================================================================================================
// Computes the factorial of a non-negative integer.
// Args:
//   n: The input integer. Must be non-negative.
// Returns:
//   The factorial of n, or 0 if n is negative (error case)
//========================================================================================================
int funct2(int n);

* make sure a carriage return is present after each function documentation and function definition as shown above.

## FUNCTIONAL SEPARATION ##
////////////////////////////////////////// Connection Management ///////////////////////////////////////////

* should match the line length of this line, balanced on both side of the phrase as equal as possible.
//========================================================================================================



## INTERFACE DOCUMENTATION FORMAT ##

//========================================================================================================
// MCP Client interface
// Purpose: MCP client interface definitions.
//========================================================================================================