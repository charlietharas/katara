#ifndef RENDER_HISTOGRAM_H
#define RENDER_HISTOGRAM_H

#include "sim_shared.h"
#include <vector>

namespace RenderHistogram {

constexpr int HISTOGRAM_BINS = 64;

struct HistogramData {
    std::vector<int> densityHistogramBins;
    float densityHistogramMin;
    float densityHistogramMax;
    std::vector<int> velocityHistogramBins;
    float velocityHistogramMin;
    float velocityHistogramMax;
};

float computeShannonEntropy(const std::vector<int>& bins);
void computeHistograms(const ISimulator& simulator, HistogramData& data);

} // namespace RenderHistogram

#endif
