# CheckVersionSync.cmake — verifies all version strings match.
# Called from the 'check_version' CMake target.
# Usage: cmake -P CheckVersionSync.cmake

cmake_minimum_required(VERSION 3.24)

# 1. Read CMakeLists.txt version via PROJECT_VERSION (set by parent)
# When run standalone, extract from CMakeLists.txt directly.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" cmake_content)
string(REGEX MATCH "project\\(HDAW VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${cmake_content}")
set(CMAKE_VERSION_STR "${CMAKE_MATCH_1}")

# 2. Read frontend/package.json version
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/frontend/package.json" pkg_content)
string(REGEX MATCH "\"version\"[ \t]*: \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${pkg_content}")
set(PKG_VERSION_STR "${CMAKE_MATCH_1}")

# 3. Compare
message(STATUS "CMakeLists.txt version: ${CMAKE_VERSION_STR}")
message(STATUS "frontend/package.json version: ${PKG_VERSION_STR}")

if(NOT CMAKE_VERSION_STR)
    message(FATAL_ERROR "Could not extract version from CMakeLists.txt")
endif()

if(NOT PKG_VERSION_STR)
    message(FATAL_ERROR "Could not extract version from frontend/package.json")
endif()

if(NOT CMAKE_VERSION_STR STREQUAL PKG_VERSION_STR)
    message(FATAL_ERROR
        "Version mismatch!\n"
        "  CMakeLists.txt:  ${CMAKE_VERSION_STR}\n"
        "  package.json:    ${PKG_VERSION_STR}\n"
        "Update both to the same version string.")
else()
    message(STATUS "Versions are in sync: ${CMAKE_VERSION_STR}")
endif()
