#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <cstdint>
#include <atomic>
#include <cstring>

namespace handtrack {

// Hand data structure matching MediaPipe output
// 21 keypoints per hand: x, y, z coordinates (normalized 0-1)
struct HandData {
    float keypoints[21][3];  // [keypoint_index][coord] where coord is 0=x, 1=y, 2=z
    float confidence;
    uint8_t handedness;      // 0=left, 1=right, 255=unknown
    uint8_t padding[3];      // Align to 8 bytes
};

// Shared memory layout for zero-copy data passing between JS and WASM
// Total size: metadata + video frame + hand data + sync flags
struct SharedMemoryLayout {
    // Metadata section (64 bytes)
    uint32_t frameWidth;
    uint32_t frameHeight;
    uint32_t timestamp;
    uint32_t frameNumber;
    uint8_t padding[48];

    // Video frame data starts at offset 64
    // Stored as RGBA (4 bytes per pixel)
    uint8_t videoFrameData[];  // Flexible array member

    // Hand data section (after video frame)
    // Located at: FRAME_DATA_OFFSET + MAX_FRAME_SIZE
    // HandData hands[2];

    // Sync flags section (at end of buffer)
    // std::atomic<uint32_t> frameReady;
    // std::atomic<uint32_t> frameConsumed;
};

class SharedMemory {
public:
    // Maximum frame size: 1920x1080 RGBA = ~8.3MB
    // Using 640x480 RGBA = ~1.2MB as default
    static constexpr size_t MAX_FRAME_WIDTH = 1920;
    static constexpr size_t MAX_FRAME_HEIGHT = 1080;
    static constexpr size_t MAX_FRAME_SIZE = MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * 4;

    // Memory layout offsets
    static constexpr size_t METADATA_SIZE = 64;
    static constexpr size_t FRAME_DATA_OFFSET = METADATA_SIZE;
    static constexpr size_t HANDS_DATA_OFFSET = FRAME_DATA_OFFSET + MAX_FRAME_SIZE;
    static constexpr size_t FRAME_READY_OFFSET = HANDS_DATA_OFFSET + sizeof(HandData) * 2;
    static constexpr size_t FRAME_CONSUMED_OFFSET = FRAME_READY_OFFSET + sizeof(uint32_t);
    static constexpr size_t TOTAL_SIZE = FRAME_CONSUMED_OFFSET + sizeof(uint32_t);

    SharedMemory();
    ~SharedMemory();

    // Initialize with external buffer (from WASM memory)
    void init(void* externalBuffer, size_t size);

    // Access the raw buffer
    void* getBuffer() { return buffer; }
    const void* getBuffer() const { return buffer; }
    size_t getSize() const { return bufferSize; }

    // Frame metadata accessors
    uint32_t getFrameWidth() const;
    uint32_t getFrameHeight() const;
    uint32_t getTimestamp() const;
    uint32_t getFrameNumber() const;

    // Frame data access
    const uint8_t* getFrameData() const;
    size_t getFrameDataSize() const;

    // Hand data access
    const HandData* getHandsData() const;
    size_t getNumHands() const;  // Returns actual number of hands detected

    // Synchronization
    bool isFrameReady() const;
    void setFrameConsumed();
    bool wasFrameConsumed() const;

    // Get atomic flag pointers for direct access
    std::atomic<uint32_t>* getFrameReadyFlag();
    std::atomic<uint32_t>* getFrameConsumedFlag();

private:
    uint8_t* buffer;
    size_t bufferSize;
    bool ownsBuffer;

    // Helper to get pointers to specific sections
    template<typename T>
    T* getOffset(size_t offset) {
        return reinterpret_cast<T*>(static_cast<uint8_t*>(buffer) + offset);
    }

    template<typename T>
    const T* getOffset(size_t offset) const {
        return reinterpret_cast<const T*>(static_cast<const uint8_t*>(buffer) + offset);
    }
};

} // namespace handtrack

#endif // SHARED_MEMORY_H
