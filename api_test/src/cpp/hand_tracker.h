#ifndef HAND_TRACKER_H
#define HAND_TRACKER_H

#include "shared_memory.h"
#include <vector>

namespace handtrack {

// Simple wrapper for hand tracking data processing
// The actual detection is done by MediaPipe in JavaScript,
// this class just processes the results from shared memory
class HandTracker {
public:
    HandTracker() = default;
    ~HandTracker() = default;

    // Process hand data from shared memory
    void processHands(const HandData* hands, size_t numHands);

    // Get processed hand data
    const std::vector<HandData>& getHands() const { return hands; }
    size_t getNumHands() const { return hands.size(); }

private:
    std::vector<HandData> hands;
};

} // namespace handtrack

#endif // HAND_TRACKER_H
