include(FetchContent)

# Fetch JUCE 8
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.0
)

FetchContent_MakeAvailable(juce)

# CLAP plugin hosting via clap-juce-extensions
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    # Pinned to a fixed commit (not a moving branch) so FetchContent does not
    # re-fetch/re-checkout on every configure, which would force recompiles.
    # Bump deliberately after verifying the upstream change is compatible.
    GIT_TAG 54b3c3268ab6721a7afeef813c9e1ce43a3d0fcd
    SOURCE_DIR "${CMAKE_BINARY_DIR}/clap-juce-extensions-src"
)
FetchContent_MakeAvailable(clap-juce-extensions)
