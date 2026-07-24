# Standalone aubio build for HDAW.
# Replaces aubio's own CMakeLists.txt which depends on PkgConfig.
# Builds only the core DSP library (no I/O backends, no external FFT).
cmake_minimum_required(VERSION 3.24)
project(aubio_build LANGUAGES C)

set(CMAKE_C_STANDARD 99)

# Generate config.h for the target platform.
include(CheckIncludeFile)
check_include_file("stdlib.h"  HAVE_STDLIB_H)
check_include_file("stdio.h"   HAVE_STDIO_H)
check_include_file("math.h"    HAVE_MATH_H)
check_include_file("string.h"  HAVE_STRING_H)
check_include_file("errno.h"   HAVE_ERRNO_H)
check_include_file("limits.h"  HAVE_LIMITS_H)
check_include_file("stdarg.h"  HAVE_STDARG_H)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

# Core source files — BPM detection + supporting DSP.
# Excludes: io/ (needs sndfile/avcodec), effects/ (needs rubberband),
# synth/ (not needed), spectral/*fftw* and spectral/*ipp* (external FFT).
set(AUBIO_SOURCES
    src/fvec.c
    src/fmat.c
    src/cvec.c
    src/lvec.c
    src/vecutils.c
    src/mathutils.c
    src/utils/log.c
    src/utils/windll.c
    src/utils/hist.c
    src/utils/scale.c
    src/utils/parameter.c
    src/utils/strutils.c
    src/spectral/filterbank_mel.c
    src/spectral/dct_ooura.c
    src/spectral/statistics.c
    src/spectral/tss.c
    src/spectral/specdesc.c
    src/spectral/awhitening.c
    src/spectral/filterbank.c
    src/spectral/dct_accelerate.c
    src/spectral/fft.c
    src/spectral/dct.c
    src/spectral/mfcc.c
    src/spectral/phasevoc.c
    src/spectral/dct_plain.c
    src/spectral/ooura_fft8g.c
    src/temporal/biquad.c
    src/temporal/filter.c
    src/temporal/a_weighting.c
    src/temporal/c_weighting.c
    src/pitch/pitchspecacf.c
    src/pitch/pitchschmitt.c
    src/pitch/pitchmcomb.c
    src/pitch/pitchyinfast.c
    src/pitch/pitchyin.c
    src/pitch/pitchfcomb.c
    src/pitch/pitchyinfft.c
    src/pitch/pitch.c
    src/notes/notes.c
    src/tempo/tempo.c
    src/tempo/beattracking.c
    src/onset/peakpicker.c
    src/onset/onset.c
)

add_library(aubio STATIC ${AUBIO_SOURCES})

target_include_directories(aubio
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(aubio PRIVATE /W0)
else()
    target_compile_options(aubio PRIVATE -w)
endif()

target_compile_definitions(aubio PRIVATE HAVE_AUBIO)

if(NOT WIN32)
    target_link_libraries(aubio PRIVATE m)
endif()
