include(FetchContent)

# aubio — lightweight C library for audio labeling (onset detection, tempo).
# Used by BpmDetector for automatic BPM extraction on import.
# NOTE: upstream aubio uses waf, not CMake.  We FetchContent the sources
#       and build a minimal static library from the core modules only
#       (no sndfile / avcodec / jack I/O backends).
FetchContent_Declare(
    aubio
    GIT_REPOSITORY https://github.com/aubio/aubio.git
    GIT_TAG        0.4.4
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_BINARY_DIR}/_aubio"
)
FetchContent_GetProperties(aubio)
if(NOT aubio_POPULATED)
    FetchContent_Populate(aubio)
endif()

set(_AUBIO_SRC "${aubio_SOURCE_DIR}/src")

# Core source files (no external-library I/O backends).
set(_AUBIO_SOURCES
    ${_AUBIO_SRC}/cvec.c
    ${_AUBIO_SRC}/fmat.c
    ${_AUBIO_SRC}/fvec.c
    ${_AUBIO_SRC}/lvec.c
    ${_AUBIO_SRC}/mathutils.c
    ${_AUBIO_SRC}/vecutils.c
    # spectral
    ${_AUBIO_SRC}/spectral/fft.c
    ${_AUBIO_SRC}/spectral/filterbank.c
    ${_AUBIO_SRC}/spectral/filterbank_mel.c
    ${_AUBIO_SRC}/spectral/mfcc.c
    ${_AUBIO_SRC}/spectral/ooura_fft8g.c
    ${_AUBIO_SRC}/spectral/phasevoc.c
    ${_AUBIO_SRC}/spectral/specdesc.c
    ${_AUBIO_SRC}/spectral/statistics.c
    ${_AUBIO_SRC}/spectral/tss.c
    # onset
    ${_AUBIO_SRC}/onset/onset.c
    ${_AUBIO_SRC}/onset/peakpicker.c
    # pitch
    ${_AUBIO_SRC}/pitch/pitch.c
    ${_AUBIO_SRC}/pitch/pitchfcomb.c
    ${_AUBIO_SRC}/pitch/pitchmcomb.c
    ${_AUBIO_SRC}/pitch/pitchschmitt.c
    ${_AUBIO_SRC}/pitch/pitchspecacf.c
    ${_AUBIO_SRC}/pitch/pitchyin.c
    ${_AUBIO_SRC}/pitch/pitchyinfft.c
    # tempo
    ${_AUBIO_SRC}/tempo/beattracking.c
    ${_AUBIO_SRC}/tempo/tempo.c
    # temporal
    ${_AUBIO_SRC}/temporal/a_weighting.c
    ${_AUBIO_SRC}/temporal/biquad.c
    ${_AUBIO_SRC}/temporal/c_weighting.c
    ${_AUBIO_SRC}/temporal/filter.c
    ${_AUBIO_SRC}/temporal/resampler.c
    # utils
    ${_AUBIO_SRC}/utils/hist.c
    ${_AUBIO_SRC}/utils/log.c
    ${_AUBIO_SRC}/utils/parameter.c
    ${_AUBIO_SRC}/utils/scale.c
    # notes
    ${_AUBIO_SRC}/notes/notes.c
    # synth
    ${_AUBIO_SRC}/synth/sampler.c
    ${_AUBIO_SRC}/synth/wavetable.c
)

add_library(aubio STATIC ${_AUBIO_SOURCES})
target_include_directories(aubio PUBLIC  "${_AUBIO_SRC}")
target_include_directories(aubio PRIVATE "${_AUBIO_SRC}")
target_compile_definitions(aubio PRIVATE
    HAVE_STDLIB_H=1
    HAVE_STDIO_H=1
    HAVE_MATH_H=1
    HAVE_STRING_H=1
    HAVE_LIMITS_H=1
    HAVE_STDARG_H=1
    HAVE_C99_VARARGS_MACROS=1
    # Use built-in Ooura FFT (no fftw3 dependency).
    HAVE_OOURA=1
)

# Silence warnings in third-party code.
if(MSVC)
    target_compile_options(aubio PRIVATE /W0)
else()
    target_compile_options(aubio PRIVATE -w)
endif()

set_target_properties(aubio PROPERTIES
    C_STANDARD   99
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Expose a stable alias for HDAW consumers.
add_library(HDAW::aubio ALIAS aubio)
