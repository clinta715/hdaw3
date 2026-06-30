#pragma once
#include <atomic>
#include <cstdint>

namespace proxy {

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(uint32_t capacity)
        : cap(nextPow2(capacity))
        , mask(cap - 1)
    {
        buffer = new T[cap];
    }

    ~RingBuffer() { delete[] buffer; }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool write(const T* data, uint32_t count) {
        uint32_t w = writePos.load(std::memory_order_relaxed);
        uint32_t r = readPos.load(std::memory_order_acquire);
        uint32_t available = cap - (w - r);
        if (count > available) return false;

        for (uint32_t i = 0; i < count; ++i)
            buffer[(w + i) & mask] = data[i];

        writePos.store(w + count, std::memory_order_release);
        return true;
    }

    bool read(T* data, uint32_t count) {
        uint32_t r = readPos.load(std::memory_order_relaxed);
        uint32_t w = writePos.load(std::memory_order_acquire);
        uint32_t available = w - r;
        if (count > available) return false;

        for (uint32_t i = 0; i < count; ++i)
            data[i] = buffer[(r + i) & mask];

        readPos.store(r + count, std::memory_order_release);
        return true;
    }

    uint32_t availableToRead() const {
        return writePos.load(std::memory_order_acquire) -
               readPos.load(std::memory_order_relaxed);
    }

    uint32_t availableToWrite() const {
        return cap - availableToRead();
    }

private:
    static uint32_t nextPow2(uint32_t v) {
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4;
        v |= v >> 8; v |= v >> 16;
        return v + 1;
    }

    T* buffer;
    uint32_t cap;
    uint32_t mask;
    alignas(64) std::atomic<uint32_t> writePos{0};
    alignas(64) std::atomic<uint32_t> readPos{0};
};

} // namespace proxy
