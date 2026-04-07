cmake_minimum_required(VERSION 3.14)
project(ClawAgent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
endif()

# Dependencies
option(NO_CURL "Build without curl (for Android without prebuilt curl)" OFF)

if(NO_CURL)
    # Android build without curl - define a stub
    add_definitions(-DNO_CURL=1)
    set(CURL_LIBRARIES "")
elseif(CURL_LIBRARY AND CURL_INCLUDE_DIR)
    # Use custom curl library (for Android cross-compilation)
    add_definitions(-DCURL_STATIC=1)
    set(CURL_LIBRARIES "${CURL_LIBRARY};${ZLIB_LIBRARY}")
    set(CURL_INCLUDE_DIRS "${CURL_INCLUDE_DIR};${ZLIB_INCLUDE_DIR}")
    include_directories(${CURL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
else()
    find_package(CURL REQUIRED)
    set(CURL_LIBRARIES CURL::libcurl)
endif()

# Threading
find_package(Threads REQUIRED)

# nlohmann/json (single header file)
# Include thirdparty directory so that #include <nlohmann/json.hpp> works
include_directories(${CMAKE_SOURCE_DIR}/thirdparty)

# Source files
if(NO_CURL)
    # Android build without curl - use stub
    set(LLM_CLIENT_SRC src/llm/LLMClientStub.cpp)
else()
    set(LLM_CLIENT_SRC src/llm/LLMClient.cpp)
endif()

set(SOURCES
    src/main.cpp
    src/ClawAgent.cpp
    src/config/ConfigManager.cpp
    ${LLM_CLIENT_SRC}
    src/message/Message.cpp
    src/message/MessageManager.cpp
    src/tools/ToolManager.cpp
    src/agent/AgentRuntime.cpp
    src/utils/Logger.cpp
)

# Header files
set(HEADERS
    include/ClawAgent.hpp
    include/config/ConfigManager.hpp
    include/llm/LLMClient.hpp
    include/message/Message.hpp
    include/message/MessageManager.hpp
    include/tools/ToolManager.hpp
    include/agent/AgentRuntime.hpp
    include/utils/Logger.hpp
)

# Create executable
add_executable(clawagent ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(clawagent PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty
)

# Link libraries
target_link_libraries(clawagent PRIVATE
    ${CURL_LIBRARIES}
    Threads::Threads
)

# Install target
install(TARGETS clawagent DESTINATION bin)

# ============ Google Test ============
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()

    # Fetch Google Test from GitHub
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    # Test sources
    set(TEST_SOURCES
        tests/test_config.cpp
        tests/test_message.cpp
        tests/test_tool.cpp
        src/config/ConfigManager.cpp
        src/message/Message.cpp
        src/message/MessageManager.cpp
        src/tools/ToolManager.cpp
        src/utils/Logger.cpp
    )

    add_executable(clawagent_tests ${TEST_SOURCES})
    target_include_directories(clawagent_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty
    )
    target_link_libraries(clawagent_tests PRIVATE
        gtest_main
        ${CURL_LIBRARIES}
        Threads::Threads
    )

    include(GoogleTest)
    gtest_discover_tests(clawagent_tests)
endif()
