include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(scan_hang_helper
    scan_hang_helper/main.cpp
)

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
    integration/mcp/mcp_coverage_test.cpp
    unit/proxy/common_test.cpp
    unit/proxy/pipe_test.cpp
    unit/proxy/shm_test.cpp
    unit/proxy/crash_recovery_test.cpp
    unit/engine/audioengine_read_facade_test.cpp
    unit/engine/track_fx_slot_debug_test.cpp
    unit/engine/track_fx_slot_show_editor_test.cpp
    unit/engine/isolated_scanner_test.cpp
    unit/engine/plugin_state_save_load_test.cpp
    unit/engine/plugin_identifier_resolution_test.cpp
    unit/engine/preset_cache_test.cpp
    unit/engine/plugin_manager_scan_test.cpp
    unit/engine/markers_test.cpp
    unit/engine/stretch_test.cpp
    unit/engine/modulation_test.cpp
    unit/engine/clip_source_processor_test.cpp
    unit/engine/sampler_sound_test.cpp
    unit/engine/sampler_interpolator_test.cpp
    unit/engine/ahdsr_envelope_test.cpp
    unit/engine/sampler_voice_test.cpp
    unit/engine/sampler_engine_test.cpp
    unit/engine/fm_synth_test.cpp
    unit/engine/dx7_sysex_import_test.cpp
    unit/engine/slice_detector_test.cpp
    unit/engine/sampler_fxslot_test.cpp
    unit/engine/realtime_safety_test.cpp
    unit/engine/audio_pool_dedup_test.cpp
    unit/engine/track_fx_rebuild_race_test.cpp
    unit/engine/render_sequence_release_test.cpp
    unit/engine/streaming_clip_source_test.cpp
    unit/engine/streaming_pool_test.cpp
    unit/engine/clip_streaming_e2e_test.cpp
    unit/engine/track_mixer_state_test.cpp
    unit/engine/transport_manager_test.cpp
    unit/engine/midi_clip_processor_test.cpp
    unit/engine/operator_logic_test.cpp
    unit/engine/midi_fx_test.cpp
    unit/engine/midi_fx_automation_test.cpp
    unit/engine/modulation_fx_test.cpp
    unit/engine/transient_detector_test.cpp
    unit/engine/region_clipboard_test.cpp
    unit/engine/bpm_detector_test.cpp
    unit/engine/generative_test.cpp
    unit/engine/phrase_generator_test.cpp
    unit/engine/phrase_generator_new_styles_test.cpp
    unit/engine/envelope_generator_test.cpp
    unit/engine/arrangement_generator_test.cpp
    unit/engine/rhythm_pattern_generator_test.cpp
    unit/engine/pattern_library_test.cpp
    unit/engine/arranger_test.cpp
    unit/engine/bar_snap_test.cpp
    unit/engine/ghost_clips_test.cpp
    unit/engine/merge_clips_test.cpp
    unit/engine/midi_import_test.cpp
    unit/engine/midi_analyzer_test.cpp
    unit/engine/ripple_delete_test.cpp
    unit/engine/region_ops_test.cpp
    unit/engine/rpc_surface_test.cpp
    unit/engine/project_metadata_test.cpp
    unit/engine/crossfade_test.cpp
    unit/engine/incremental_routing_spike_test.cpp
    unit/engine/incremental_routing_remove_move_test.cpp
    unit/engine/incremental_routing_engine_test.cpp
    unit/engine/incremental_routing_ab_test.cpp
    unit/engine/automation_test.cpp
    unit/engine/automation_units_test.cpp
    unit/engine/automation_mode_test.cpp
    unit/engine/export_volume_bypass_test.cpp
    unit/engine/export_bake_timeout_test.cpp
    unit/engine/instrument_part_test.cpp
    unit/engine/audition_test.cpp
    unit/engine/verify_part_test.cpp
    unit/engine/offline_render_exclusion_test.cpp
    unit/engine/master_gain_test.cpp
    unit/engine/clap_preset_probe_test.cpp
    unit/engine/clap_preset_database_test.cpp
    unit/engine/clap_program_test.cpp
    unit/engine/envelope_generation_test.cpp
    unit/engine/session_test.cpp
    unit/engine/meter_test.cpp
    unit/engine/punch_test.cpp
    unit/engine/send_test.cpp
    unit/engine/file_library_test.cpp
    unit/engine/library_clusterer_test.cpp
    unit/common/read_model_test.cpp
    unit/common/commands_test.cpp
    unit/common/message_pump_test.cpp
    unit/model/project_model_test.cpp
    unit/model/clip_slicing_test.cpp
    unit/frontend/frontend_server_test.cpp
    unit/frontend/ghost_clips_rpc_test.cpp
    unit/frontend/envelope_generation_rpc_test.cpp
    unit/frontend/rhythm_generation_rpc_test.cpp
    unit/frontend/tree_delta_accumulator_test.cpp
    integration/proxy/isolation_integration_test.cpp
    integration/proxy/namespace_collision_test.cpp
)

target_include_directories(hdaw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(hdaw_tests PRIVATE /FI${CMAKE_SOURCE_DIR}/tests/cstdio_compat.h)
# clap_preset_probe_test.cpp installs an SEH translator around plugin init()
# (containment guard); _set_se_translator only takes effect with /EHa. The
# rest of the suite keeps the default exception-handling mode.
if(MSVC)
    set_source_files_properties(unit/engine/clap_preset_probe_test.cpp
        PROPERTIES COMPILE_OPTIONS "/EHa")
endif()
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
