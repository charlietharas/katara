#include "render_histogram.h"
#include <algorithm>
#include <cmath>

namespace RenderHistogram {

float computeShannonEntropy(const std::vector<int>& bins) {
    long long totalCount = 0;
    for (int count : bins) {
        if (count > 0) {
            totalCount += count;
        }
    }
    if (totalCount <= 0) {
        return 0.0f;
    }

    float entropy = 0.0f;
    const float invTotal = 1.0f / static_cast<float>(totalCount);
    for (int count : bins) {
        if (count <= 0) {
            continue;
        }
        const float probability = static_cast<float>(count) * invTotal;
        entropy -= probability * std::log2(probability);
    }
    return entropy;
}

void computeHistograms(const ISimulator& simulator, HistogramData& data) {
    const auto& pressure = simulator.getPressure();
    const auto& solid = simulator.getSolid();
    const auto& velocityX = simulator.getVelocityX();
    const auto& velocityY = simulator.getVelocityY();
    int gridX = simulator.gridX;
    int gridY = simulator.gridY;

    // density histogram (using pressure)
    bool first = true;
    for (int i = 0; i < gridX * gridY; i++) {
        if (solid[i] != 0.0f) { // only fluid cells
            if (first) {
                data.densityHistogramMin = pressure[i];
                data.densityHistogramMax = pressure[i];
                first = false;
            } else {
                data.densityHistogramMin = std::min(data.densityHistogramMin, pressure[i]);
                data.densityHistogramMax = std::max(data.densityHistogramMax, pressure[i]);
            }
        }
    }
    std::fill(data.densityHistogramBins.begin(), data.densityHistogramBins.end(), 0);

    if (data.densityHistogramMax > data.densityHistogramMin) {
        float binWidth = (data.densityHistogramMax - data.densityHistogramMin) / HISTOGRAM_BINS;
        for (int i = 0; i < gridX * gridY; i++) {
            if (solid[i] != 0.0f) { // only fluid cells
                int bin = static_cast<int>((pressure[i] - data.densityHistogramMin) / binWidth);
                bin = std::max(0, std::min(HISTOGRAM_BINS - 1, bin));
                data.densityHistogramBins[bin]++;
            }
        }
    }

    // velocity histogram
    first = true;
    for (int i = 0; i < gridX * gridY; i++) {
        if (solid[i] != 0.0f) { // only fluid cells
            float velMagnitude = std::sqrt(velocityX[i] * velocityX[i] + velocityY[i] * velocityY[i]);
            if (first) {
                data.velocityHistogramMin = velMagnitude;
                data.velocityHistogramMax = velMagnitude;
                first = false;
            } else {
                data.velocityHistogramMin = std::min(data.velocityHistogramMin, velMagnitude);
                data.velocityHistogramMax = std::max(data.velocityHistogramMax, velMagnitude);
            }
        }
    }

    std::fill(data.velocityHistogramBins.begin(), data.velocityHistogramBins.end(), 0);

    if (data.velocityHistogramMax > data.velocityHistogramMin) {
        float binWidth = (data.velocityHistogramMax - data.velocityHistogramMin) / HISTOGRAM_BINS;
        for (int i = 0; i < gridX * gridY; i++) {
            if (solid[i] != 0.0f) { // only fluid cells
                float velMagnitude = std::sqrt(velocityX[i] * velocityX[i] + velocityY[i] * velocityY[i]);
                int bin = static_cast<int>((velMagnitude - data.velocityHistogramMin) / binWidth);
                bin = std::max(0, std::min(HISTOGRAM_BINS - 1, bin));
                data.velocityHistogramBins[bin]++;
            }
        }
    }
}

} // namespace RenderHistogram
