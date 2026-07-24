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
    integration/mcp/mcp_functionality_test.cpp
    unit/proxy/common_test.cpp
    unit/proxy/ring_buffer_test.cpp
    unit/proxy/pipe_test.cpp
    unit/proxy/shm_test.cpp
    unit/engine/track_fx_slot_debug_test.cpp
    unit/engine/track_fx_slot_show_editor_test.cpp
    unit/engine/isolated_scanner_test.cpp
    unit/engine/plugin_state_save_load_test.cpp
    unit/engine/markers_test.cpp
    unit/engine/stretch_test.cpp
    unit/engine/transient_detector_test.cpp
    unit/engine/modulation_test.cpp
    unit/engine/clip_source_processor_test.cpp
    unit/engine/transient_detector_test.cpp
    unit/engine/region_clipboard_test.cpp
    unit/engine/bpm_detector_test.cpp
    unit/engine/bar_snap_test.cpp
    unit/engine/ghost_clips_test.cpp
    unit/common/read_model_test.cpp
    unit/common/commands_test.cpp
    unit/model/project_model_test.cpp
    unit/model/clip_slicing_test.cpp
    unit/frontend/frontend_server_test.cpp
    unit/frontend/ghost_clips_rpc_test.cpp
)

target_include_directories(hdaw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hdaw_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test
    HDAW_lib
    GTest::gtest_main
)
add_test(NAME hdaw_tests COMMAND hdaw_tests)

hdaw_deploy_qt(hdaw_tests)
