# RunTimeSync.cmake - pre-build WSL time-sync hook.
# Invoked by the 'hdaw_time_sync' ALL custom target (see CMakeLists.txt) as:
#   ${CMAKE_COMMAND} -P cmake/RunTimeSync.cmake
# Runs scripts\\time-sync.cmd via cmd.exe with quoting handled by CMake
# (no shell-level nesting, so paths with spaces are safe). The hook NEVER
# fails the build: the .cmd/.sh exit 0 on every path unless the caller sets
# HDAW_TIME_SYNC_STRICT=1 explicitly (interactive use only).

cmake_minimum_required(VERSION 3.24)

get_filename_component(HDAW_SYNC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
execute_process(
    COMMAND cmd.exe /C call "${HDAW_SYNC_ROOT}/scripts/time-sync.cmd"
    RESULT_VARIABLE _sync_rc
)
if(NOT _sync_rc EQUAL 0)
    message(WARNING "hdaw time-sync hook exited ${_sync_rc}; continuing the build")
endif()
