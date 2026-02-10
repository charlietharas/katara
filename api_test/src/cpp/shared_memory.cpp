#include "shared_memory.h"

namespace handtrack {

SharedMemory::SharedMemory()
    : buffer(nullptr)
    , bufferSize(0)
    , ownsBuffer(true)
{
}

SharedMemory::~SharedMemory() {
    if (ownsBuffer && buffer) {
        delete[] static_cast<uint8_t*>(buffer);
    }
}

void SharedMemory::init(void* externalBuffer, size_t size) {
    if (ownsBuffer && buffer) {
        delete[] static_cast<uint8_t*>(buffer);
    }

    buffer = static_cast<uint8_t*>(externalBuffer);
    bufferSize = size;
    ownsBuffer = false;
}

uint32_t SharedMemory::getFrameWidth() const {
    return *getOffset<uint32_t>(0);
}

uint32_t SharedMemory::getFrameHeight() const {
    return *getOffset<uint32_t>(4);
}

uint32_t SharedMemory::getTimestamp() const {
    return *getOffset<uint32_t>(8);
}

uint32_t SharedMemory::getFrameNumber() const {
    return *getOffset<uint32_t>(12);
}

const uint8_t* SharedMemory::getFrameData() const {
    return getOffset<uint8_t>(FRAME_DATA_OFFSET);
}

size_t SharedMemory::getFrameDataSize() const {
    return getFrameWidth() * getFrameHeight() * 4;  // RGBA
}

const HandData* SharedMemory::getHandsData() const {
    return getOffset<HandData>(HANDS_DATA_OFFSET);
}

size_t SharedMemory::getNumHands() const {
    // Count hands with confidence > 0
    const HandData* hands = getHandsData();
    size_t count = 0;
    for (int i = 0; i < 2; i++) {
        if (hands[i].confidence > 0.0f) {
            count++;
        }
    }
    return count;
}

bool SharedMemory::isFrameReady() const {
    const std::atomic<uint32_t>* flag = getOffset<std::atomic<uint32_t>>(FRAME_READY_OFFSET);
    return flag->load(std::memory_order_acquire) != 0;
}

void SharedMemory::setFrameConsumed() {
    std::atomic<uint32_t>* flag = getOffset<std::atomic<uint32_t>>(FRAME_CONSUMED_OFFSET);
    flag->store(1, std::memory_order_release);
}

bool SharedMemory::wasFrameConsumed() const {
    const std::atomic<uint32_t>* flag = getOffset<std::atomic<uint32_t>>(FRAME_CONSUMED_OFFSET);
    return flag->load(std::memory_order_acquire) != 0;
}

std::atomic<uint32_t>* SharedMemory::getFrameReadyFlag() {
    return getOffset<std::atomic<uint32_t>>(FRAME_READY_OFFSET);
}

std::atomic<uint32_t>* SharedMemory::getFrameConsumedFlag() {
    return getOffset<std::atomic<uint32_t>>(FRAME_CONSUMED_OFFSET);
}

} // namespace handtrack
