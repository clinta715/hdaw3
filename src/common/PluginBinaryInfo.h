#pragma once
#include <juce_core/juce_core.h>

namespace HDAW {

namespace detail {

// Stream reads are not guaranteed to fill the buffer in one call — loop
// until full or EOF. Returns true when exactly numBytes were read.
inline bool readFully(juce::InputStream& in, void* dest, size_t numBytes)
{
    auto* p = static_cast<juce::uint8*>(dest);
    size_t got = 0;
    while (got < numBytes)
    {
        const auto n = in.read(p + got, (int)(numBytes - got));
        if (n <= 0)
            return false;
        got += (size_t)n;
    }
    return true;
}

} // namespace detail

// Peers into the PE header directly on disk (no module mapping — safe for
// 32-bit images from a 64-bit host process). Returns true when the binary is
// an x86 (32-bit) image; false for 64-bit, non-PE, or unreadable files.
inline bool is32BitPluginBinary(const juce::File& file)
{
#if JUCE_WINDOWS
    juce::FileInputStream in(file);
    if (!in.openedOk())
        return false;

    juce::uint8 dos[64] = {};
    if (!detail::readFully(in, dos, sizeof(dos)))
        return false;
    if (dos[0] != 'M' || dos[1] != 'Z')
        return false;

    const juce::uint32 peOffset = (juce::uint32)dos[0x3C]
        | ((juce::uint32)dos[0x3D] << 8)
        | ((juce::uint32)dos[0x3E] << 16)
        | ((juce::uint32)dos[0x3F] << 24);

    if (!in.setPosition(peOffset))
        return false;
    juce::uint8 pe[6] = {};
    if (!detail::readFully(in, pe, sizeof(pe)))
        return false;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0)
        return false;

    const juce::uint16 machine = (juce::uint16)pe[4] | ((juce::uint16)pe[5] << 8);
    return machine == 0x14C; // IMAGE_FILE_MACHINE_I386
#else
    juce::ignoreUnused(file);
    return false;
#endif
}

} // namespace HDAW