#include "hand_tracker.h"

namespace handtrack {

void HandTracker::processHands(const HandData* hands, size_t numHands) {
    this->hands.clear();

    for (size_t i = 0; i < numHands; i++) {
        if (hands[i].confidence > 0.0f) {
            this->hands.push_back(hands[i]);
        }
    }
}

} // namespace handtrack
