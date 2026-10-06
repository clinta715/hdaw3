# CheckSkillsMirror.cmake — verifies the two skill trees are byte-identical.
#
# `.agents/skills/` is the LIVE tree the skill loader reads; `docs/skills/` is
# the doc-map mirror. They are byte-identical by convention but nothing
# enforced it: the trees silently drifted (each held Linux-port content the
# other lacked) and had to be hand-merged. This guard FAILS the build on drift.
#
# Called from the 'check_skills_mirror' CMake target.
# Usage: cmake -P cmake/CheckSkillsMirror.cmake
#
# Read-only: no files are written.

cmake_minimum_required(VERSION 3.24)

# Script mode does not reliably define CMAKE_CURRENT_SOURCE_DIR; derive the
# repo root from this script's own location (<repo>/cmake/CheckSkillsMirror.cmake).
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(A "${REPO_ROOT}/.agents/skills")
set(B "${REPO_ROOT}/docs/skills")

foreach(_dir "${A}" "${B}")
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "Missing skill tree: ${_dir}")
    endif()
endforeach()

file(GLOB_RECURSE A_FILES RELATIVE "${A}" "${A}/*")
file(GLOB_RECURSE B_FILES RELATIVE "${B}" "${B}/*")
list(SORT A_FILES)
list(SORT B_FILES)

set(_fix "Make the two trees identical (edit BOTH copies, or mirror one tree onto the other).")

# --- 1. Same file LIST? ------------------------------------------------------
set(A_ONLY "")
set(B_ONLY "")
foreach(_f IN LISTS A_FILES)
    if(NOT _f IN_LIST B_FILES)
        list(APPEND A_ONLY "${_f}")
    endif()
endforeach()
foreach(_f IN LISTS B_FILES)
    if(NOT _f IN_LIST A_FILES)
        list(APPEND B_ONLY "${_f}")
    endif()
endforeach()

if(A_ONLY OR B_ONLY)
    set(_msg "Skills mirror DRIFT: the two trees list different files.")
    if(A_ONLY)
        string(REPLACE ";" "\n    " _a "${A_ONLY}")
        string(APPEND _msg "\n  Only in .agents/skills:\n    ${_a}")
    endif()
    if(B_ONLY)
        string(REPLACE ";" "\n    " _b "${B_ONLY}")
        string(APPEND _msg "\n  Only in docs/skills:\n    ${_b}")
    endif()
    string(APPEND _msg "\n${_fix}")
    message(FATAL_ERROR "${_msg}")
endif()

# --- 2. Same CONTENT for every common file? ----------------------------------
set(DIFF_FILES "")
foreach(_f IN LISTS A_FILES)
    file(READ "${A}/${_f}" _a_content)
    file(READ "${B}/${_f}" _b_content)
    if(NOT _a_content STREQUAL _b_content)
        list(APPEND DIFF_FILES "${_f}")
    endif()
endforeach()

if(DIFF_FILES)
    set(_msg "Skills mirror DRIFT: file contents differ between .agents/skills and docs/skills.")
    string(REPLACE ";" "\n    " _d "${DIFF_FILES}")
    string(APPEND _msg "\n  Differing files:\n    ${_d}")

    # Show the first differing hunk (2 lines of context) of the first file.
    list(GET DIFF_FILES 0 _first)
    file(READ "${A}/${_first}" _a_content)
    file(READ "${B}/${_first}" _b_content)
    string(REPLACE "\r\n" "\n" _a_content "${_a_content}")
    string(REPLACE "\r\n" "\n" _b_content "${_b_content}")
    string(REPLACE "\n" ";" _a_lines "${_a_content}")
    string(REPLACE "\n" ";" _b_lines "${_b_content}")
    list(LENGTH _a_lines _a_n)
    list(LENGTH _b_lines _b_n)

    # First differing line index (0-based); -1 when one file is a prefix of the other.
    set(_line 0)
    set(_first_diff -1)
    while(_line LESS _a_n AND _line LESS _b_n)
        list(GET _a_lines ${_line} _la)
        list(GET _b_lines ${_line} _lb)
        if(NOT _la STREQUAL _lb)
            set(_first_diff ${_line})
            break()
        endif()
        math(EXPR _line "${_line} + 1")
    endwhile()
    if(_first_diff EQUAL -1 AND NOT _a_n EQUAL _b_n)
        if(_a_n LESS _b_n)
            set(_first_diff ${_a_n})
        else()
            set(_first_diff ${_b_n})
        endif()
    endif()

    if(_first_diff GREATER -1)
        math(EXPR _from "${_first_diff} - 2")
        if(_from LESS 0)
            set(_from 0)
        endif()
        math(EXPR _to "${_first_diff} + 2")
        string(APPEND _msg
            "\n  First difference in '${_first}' at line ${_first_diff} (0-based):")
        foreach(_side A B)
            if(_side STREQUAL "A")
                set(_lines "${_a_lines}")
                set(_name ".agents/skills")
            else()
                set(_lines "${_b_lines}")
                set(_name "docs/skills")
            endif()
            list(LENGTH _lines _n)
            string(APPEND _msg "\n    ${_name}:")
            set(_i ${_from})
            while(_i LESS _to)
                if(_i LESS _n)
                    list(GET _lines ${_i} _l)
                    if(_i EQUAL _first_diff)
                        string(APPEND _msg "\n      ${_i}: ${_l}   <-- differs")
                    else()
                        string(APPEND _msg "\n      ${_i}: ${_l}")
                    endif()
                endif()
                math(EXPR _i "${_i} + 1")
            endwhile()
        endforeach()
    endif()

    string(APPEND _msg "\n${_fix}")
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH A_FILES _count)
message(STATUS "skills mirror in sync: ${_count} files")
