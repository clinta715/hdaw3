include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(hdaw_tests
    test_main.cpp
    unit/mcp/json_rpc_test.cpp
    unit/mcp/schema_test.cpp
    unit/mcp/tool_registry_test.cpp
    unit/mcp/dry_run_test.cpp
    unit/mcp/note_id_test.cpp
    unit/mcp/transport_stdio_test.cpp
    unit/mcp/transport_http_test.cpp
    integration/mcp/mcp_server_test.cpp
    unit/proxy/common_test.cpp
    unit/proxy/ring_buffer_test.cpp
    unit/proxy/pipe_test.cpp
    unit/proxy/shm_test.cpp
    unit/engine/track_fx_slot_debug_test.cpp
)

target_include_directories(hdaw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hdaw_tests PRIVATE
    Qt6::Core
    HDAW_lib
    GTest::gtest_main
)
add_test(NAME hdaw_tests COMMAND hdaw_tests)

hdaw_deploy_qt(hdaw_tests)
