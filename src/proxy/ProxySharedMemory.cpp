#include "ProxySharedMemory.h"
#include <cstring>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace proxy {

ShmRegion::~ShmRegion() { close(); }

#if defined(_WIN32)
bool ShmRegion::create(const std::string& name, uint32_t size) {
    // Clear the last-error slot first so the ERROR_ALREADY_EXISTS check below
    // is deterministic: GetLastError is only guaranteed meaningful when it
    // distinguishes the two success outcomes documented for CreateFileMapping
    // (newly created vs. already existing).
    SetLastError(ERROR_SUCCESS);
    hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        size,
        name.c_str());
    if (hMap == INVALID_HANDLE_VALUE) return false;

    // A same-size stale region is silently OPENED and shared by
    // CreateFileMappingA — never acceptable for a fresh spawn: the parent
    // would memset the OTHER domain's/process's rings and hang its child.
    // Treat an existing region as a hard failure so callers can retry with a
    // different name instead of corrupting a live region.
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMap);
        hMap = INVALID_HANDLE_VALUE;
        return false;
    }

    basePtr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!basePtr) {
        CloseHandle(hMap);
        hMap = INVALID_HANDLE_VALUE;
        return false;
    }

    totalSize = size;
    std::memset(basePtr, 0, size);

    auto* hdr = static_cast<ShmHeader*>(basePtr);
    hdr->magic = SHM_MAGIC;
    return true;
}

bool ShmRegion::open(const std::string& name) {
    hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (hMap == INVALID_HANDLE_VALUE) return false;

    basePtr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!basePtr) {
        CloseHandle(hMap);
        hMap = INVALID_HANDLE_VALUE;
        return false;
    }

    auto* hdr = static_cast<ShmHeader*>(basePtr);
    if (hdr->magic != SHM_MAGIC) {
        close();
        return false;
    }

    totalSize = computeShmSize(hdr->numChannels, hdr->blockSize);
    return true;
}

void ShmRegion::close() {
    if (basePtr) {
        UnmapViewOfFile(basePtr);
        basePtr = nullptr;
    }
    if (hMap != INVALID_HANDLE_VALUE) {
        CloseHandle(hMap);
        hMap = INVALID_HANDLE_VALUE;
    }
}

#else // !defined(_WIN32) — POSIX shared memory (shm_open + mmap)

bool ShmRegion::create(const std::string& name, uint32_t size) {
    // O_CREAT|O_EXCL reproduces the CreateFileMappingA ERROR_ALREADY_EXISTS
    // semantics above: an existing region (a stale orphan or a same-slot
    // squatter) is a HARD failure so the caller retries with a fresh name
    // instead of memsetting another domain's live rings.
    objName = shmObjectForName(name);
    hMap = ::shm_open(objName.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (hMap == INVALID_HANDLE_VALUE) {
        objName.clear();
        return false;
    }
    ownerCreated = true;

    if (::ftruncate(hMap, static_cast<off_t>(size)) != 0) {
        ::close(hMap);
        hMap = INVALID_HANDLE_VALUE;
        ::shm_unlink(objName.c_str());
        objName.clear();
        ownerCreated = false;
        return false;
    }

    basePtr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, hMap, 0);
    if (basePtr == MAP_FAILED) {
        basePtr = nullptr;
        ::close(hMap);
        hMap = INVALID_HANDLE_VALUE;
        ::shm_unlink(objName.c_str());
        objName.clear();
        ownerCreated = false;
        return false;
    }

    totalSize = size;
    mappedLen = size;
    std::memset(basePtr, 0, size);

    auto* hdr = static_cast<ShmHeader*>(basePtr);
    hdr->magic = SHM_MAGIC;
    return true;
}

bool ShmRegion::open(const std::string& name) {
    objName = shmObjectForName(name);
    hMap = ::shm_open(objName.c_str(), O_RDWR, 0600);
    if (hMap == INVALID_HANDLE_VALUE) {
        objName.clear();
        return false;
    }

    struct stat st{};
    if (::fstat(hMap, &st) != 0 || st.st_size <= 0) {
        ::close(hMap);
        hMap = INVALID_HANDLE_VALUE;
        objName.clear();
        return false;
    }

    basePtr = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                     PROT_READ | PROT_WRITE, MAP_SHARED, hMap, 0);
    if (basePtr == MAP_FAILED) {
        basePtr = nullptr;
        ::close(hMap);
        hMap = INVALID_HANDLE_VALUE;
        objName.clear();
        return false;
    }

    auto* hdr = static_cast<ShmHeader*>(basePtr);
    mappedLen = static_cast<size_t>(st.st_size);
    if (hdr->magic != SHM_MAGIC) {
        close();
        return false;
    }

    // UNLINK-AT-FIRST-OPEN: the mapping survives via both processes' mmaps, so
    // removing the /dev/shm object here emulates Windows' last-handle-close —
    // a crashed parent cannot leak a stale object that would collide with the
    // next spawn's O_EXCL create.
    ::shm_unlink(objName.c_str());
    objName.clear();
    ownerCreated = false;

    totalSize = computeShmSize(hdr->numChannels, hdr->blockSize);
    return true;
}

void ShmRegion::close() {
    if (basePtr) {
        // Unmap the EXACT mapped length: the parent maps the worst-case size
        // while totalSize may be recomputed smaller on open.
        ::munmap(basePtr, mappedLen);
        basePtr = nullptr;
        mappedLen = 0;
    }
    if (hMap != INVALID_HANDLE_VALUE) {
        ::close(hMap);
        hMap = INVALID_HANDLE_VALUE;
    }
    if (ownerCreated) {
        ::shm_unlink(objName.c_str());
        ownerCreated = false;
    }
    objName.clear();
}

#endif // _WIN32

ShmHeader* ShmRegion::getHeader() const {
    return static_cast<ShmHeader*>(basePtr);
}

float* ShmRegion::getInputRing() const {
    if (!basePtr) return nullptr;
    return reinterpret_cast<float*>(
        static_cast<uint8_t*>(basePtr) + sizeof(ShmHeader));
}

float* ShmRegion::getOutputRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    uint32_t cap = hdr->capacity;
    if (cap == 0) return nullptr;
    return reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(getInputRing()) + cap * sizeof(float));
}

MidiEvent* ShmRegion::getMidiInRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    uint32_t cap = hdr->capacity;
    if (cap == 0) return nullptr;
    return reinterpret_cast<MidiEvent*>(
        reinterpret_cast<uint8_t*>(getOutputRing()) + cap * sizeof(float));
}

MidiEvent* ShmRegion::getMidiOutRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return getMidiInRing() + 256;
}

uint8_t* ShmRegion::getSysexInBuffer() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return reinterpret_cast<uint8_t*>(getMidiOutRing() + 256);
}

uint8_t* ShmRegion::getSysexOutBuffer() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return getSysexInBuffer() + SYSEX_BUFFER_SIZE;
}

std::atomic<uint64_t>* ShmRegion::getParamSetRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return reinterpret_cast<std::atomic<uint64_t>*>(
        getSysexOutBuffer() + SYSEX_BUFFER_SIZE);
}

std::atomic<uint64_t>* ShmRegion::getParamNotifyRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return getParamSetRing() + PARAM_RING_SIZE;
}

uint8_t* ShmRegion::getStateSetRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return reinterpret_cast<uint8_t*>(getParamNotifyRing() + PARAM_RING_SIZE);
}

bool ShmRegion::writeInput(const float* data, uint32_t count) {
    auto* hdr = getHeader();
    if (!hdr) return false;
    auto* ring = getInputRing();
    uint32_t cap = hdr->capacity;
    uint32_t w = hdr->inputWritePos.load(std::memory_order_relaxed);
    uint32_t r = hdr->inputReadPos.load(std::memory_order_acquire);
    if (count > cap - (w - r)) return false;
    for (uint32_t i = 0; i < count; ++i)
        ring[(w + i) & (cap - 1)] = data[i];
    hdr->inputWritePos.store(w + count, std::memory_order_release);
    return true;
}

bool ShmRegion::readInput(float* data, uint32_t count) {
    auto* hdr = getHeader();
    if (!hdr) return false;
    auto* ring = getInputRing();
    uint32_t cap = hdr->capacity;
    uint32_t r = hdr->inputReadPos.load(std::memory_order_relaxed);
    uint32_t w = hdr->inputWritePos.load(std::memory_order_acquire);
    if (count > w - r) return false;
    for (uint32_t i = 0; i < count; ++i)
        data[i] = ring[(r + i) & (cap - 1)];
    hdr->inputReadPos.store(r + count, std::memory_order_release);
    return true;
}

bool ShmRegion::writeOutput(const float* data, uint32_t count) {
    auto* hdr = getHeader();
    if (!hdr) return false;
    auto* ring = getOutputRing();
    uint32_t cap = hdr->capacity;
    uint32_t w = hdr->outputWritePos.load(std::memory_order_relaxed);
    uint32_t r = hdr->outputReadPos.load(std::memory_order_acquire);
    if (count > cap - (w - r)) return false;
    for (uint32_t i = 0; i < count; ++i)
        ring[(w + i) & (cap - 1)] = data[i];
    hdr->outputWritePos.store(w + count, std::memory_order_release);
    return true;
}

bool ShmRegion::readOutput(float* data, uint32_t count) {
    auto* hdr = getHeader();
    if (!hdr) return false;
    auto* ring = getOutputRing();
    uint32_t cap = hdr->capacity;
    uint32_t r = hdr->outputReadPos.load(std::memory_order_relaxed);
    uint32_t w = hdr->outputWritePos.load(std::memory_order_acquire);
    if (count > w - r) return false;
    for (uint32_t i = 0; i < count; ++i)
        data[i] = ring[(r + i) & (cap - 1)];
    hdr->outputReadPos.store(r + count, std::memory_order_release);
    return true;
}

} // namespace proxy
