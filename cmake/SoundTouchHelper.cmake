include(FetchContent)

# SoundTouch — LGPL-2.1 time/pitch stretching library.
# We build it as a SHARED library (DLL on Windows) to satisfy the LGPL
# dynamic-linking obligation cleanly (no object-file distribution required).
# `add_library(SoundTouch ...)` in SoundTouch's CMakeLists is keyword-less,
# so the global BUILD_SHARED_LIBS flag controls STATIC vs SHARED. We force
# it ON just for this subproject, then restore the caller's value.
#
# The library is isolated behind HDAW's own StretchRenderer interface
# (src/engine/StretchRenderer.h), so swapping to another backend (e.g.
# Rubber Band) is a one-file change in StretchRenderer.cpp.
set(_hdaw_prev_bsl "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    soundtouch
    GIT_REPOSITORY https://github.com/RTE-Dev/soundtouch.git
    # Pinned to a fixed commit (not a moving branch) so FetchContent does not
    # re-fetch/re-checkout on every configure, which would force recompiles.
    # Bump deliberately after verifying the upstream change is compatible.
    GIT_TAG 3e2a9e1d5c381bf6cf597c3050fbb8a2aa97fa49
    SOURCE_DIR "${CMAKE_BINARY_DIR}/soundtouch-src"
)
# SoundTouch's CMakeLists pre-dates CMake 3.5 compatibility removal; tell
# CMake to treat it as policy-aware enough to configure.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(soundtouch)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
# Restore the caller's BUILD_SHARED_LIBS so HDAW's own STATIC libs are
# not accidentally turned into a DLL by this subproject.
if(DEFINED _hdaw_prev_bsl)
    set(BUILD_SHARED_LIBS "${_hdaw_prev_bsl}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()

# Expose a stable HDAW-internal alias for the SoundTouch target.
# We use the C++ API (soundtouch::SoundTouch), so prefer the `SoundTouch`
# target. When BUILD_SHARED_LIBS is on, it's a DLL (Windows) or .so — the
# clean LGPL path. SOUNDTOUCH_DLL (the C wrapper) is not needed here.
if(TARGET SoundTouch)
    add_library(HDAW::SoundTouch ALIAS SoundTouch)
elseif(TARGET SoundTouchDLL)
    add_library(HDAW::SoundTouch ALIAS SoundTouchDLL)
endif()

# SoundTouch's CMakeLists sets SOUNDTOUCH_FLOAT_SAMPLES as a PRIVATE define
# on its target, but the sample type (SAMPLETYPE = float vs short) is part
# of the public ABI exposed via STTypes.h. Consumers MUST see the same
# sample type the library was built with, or putSamples/receiveSamples
# overload resolution breaks. Propagate it as a PUBLIC define on our alias.
if(TARGET SoundTouch)
    target_compile_definitions(SoundTouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES)
endif()
