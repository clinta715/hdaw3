# HDAW Project Instructions

## Architecture Overview
HDAW is a C++ application that integrates **Qt 6** for the user interface and **JUCE 8** for the audio engine and DSP.

### Framework Responsibilities
- **Qt 6:** Main window, panels, widgets, layout management, and user interaction.
- **JUCE 8:** Audio I/O, MIDI handling, plugin hosting, DSP primitives, and the `juce::ValueTree` for project state.

### Project Structure
- `src/main.cpp`: Entry point, initializes both Qt and JUCE.
- `src/ui/`: Qt-based UI components.
- `src/engine/`: JUCE-based audio processing logic.
- `src/model/`: Shared data models, primarily using `juce::ValueTree`.

### Build System
- CMake 3.24+ is used.
- JUCE is integrated via `FetchContent`.
- Qt 6 is found via `find_package`.
- When configuring CMake, ensure `CMAKE_PREFIX_PATH` points to your Qt installation (e.g., `C:/Qt/6.11.1/msvc2022_64`).

## Coding Conventions
- **C++ Standard:** C++20.
- **Naming:** Follow JUCE-style camelCase for methods and variables where appropriate, or standard Qt conventions for UI code.
- **Real-Time Safety:** The audio thread (inside `processBlock`) must never allocate memory, block on mutexes, or perform I/O. Use lock-free primitives (SPSC queues) for communication between UI and Engine.

## Development Workflow
1. **Research:** Map requirements to JUCE/Qt classes.
2. **Implement:** Surgical changes following the Phase-based plan.
3. **Validate:** Build and test (manual verification of UI and audio).
