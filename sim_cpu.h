#ifndef FLUID_SIMULATOR_H
#define FLUID_SIMULATOR_H

#include <vector>
#include "isimulator.h"
#include "config.h"
#include "circle_state.h"

class Simulator : public ISimulator {
public:
    Simulator(const Config& config);
    ~Simulator();

    bool init(const Config& config, const ImageData* imageData = nullptr, float aspectRatio = 1.5f) override;
    void update() override;

    void moveCircle(int newGridX, int newGridY) override;
    void updateCircles(const FingertipData* fingertips, int count) override;
    void updateLineSegments(const FingertipData* landmarks, int count) override;

    // Runtime config reload
    void updateSimParams(const Config& config) override;
    void reinitInk(const ImageData* imageData) override;
    void resetFluidState(bool clearInk = true) override;

    // fields
    const std::vector<float>& getVelocityX() const override { return x; }
    const std::vector<float>& getVelocityY() const override { return y; }
    const std::vector<float>& getPressure() const override { return p; }
    const std::vector<float>& getDensity() const override { return d; }
    const std::vector<float>& getSolid() const override { return s; }
    const std::vector<float>& getRedInk() const override { return inkRed; }
    const std::vector<float>& getGreenInk() const override { return inkGreen; }
    const std::vector<float>& getBlueInk() const override { return inkBlue; }
private:
    // config for reference in init
    const Config* config = nullptr;

    // grid params
    int resolution;
    float halfCellSize = 0.0f;

    // sim params
    float timestep;
    float density;
    float overrelaxationCoefficient;
    int projectionIters;
    bool doVorticity;
    float vorticity;
    float vorticityLen;

    // momentum transfer parameters
    float momentumTransferStrength;
    float momentumTransferRadius;
    float momentumTransferDeadZone;

    std::vector<float> x; // x vel field
    std::vector<float> y; // y vel field
    std::vector<float> s; // solid field (1 = fluid, 0 = solid)
    std::vector<float> p; // pressure field
    std::vector<float> d; // density field

    // advection util arrays
    std::vector<float> newX, newY, newD;
    std::vector<float> newInkRed, newInkGreen, newInkBlue;

    // ink diffusion
    std::vector<float> inkRed, inkGreen, inkBlue;

    // circle movement
    void updateCircle(int prevX, int prevY, int newX, int newY);
    void enforceBoundaryConditions();
    void circleMomentumTransfer();
    void updateCircleAreas(int prevX, int prevY, int newX, int newY, int prevRadius, int newRadius);

    int scaleRadiusByZ(float z);
    void clearCircleArea(int prevX, int prevY, int radius);
    void updateSingleCircle(CircleState& circle);

    bool isPointNearSegment(int px, int py, int x1, int y1, float r1, int x2, int y2, float r2);
    void updateLineSegmentSolidField(LineSegment& seg);

public:
    // image initialization helpers (public so GPU simulator can access via member)
    void initializeFromImageData(const Config& config, const ImageData* imageData);

protected:
    // sim steps
    void integrate();
    void updateProjectionIterations();
    void project();
    void extrapolate();
    void advect();
    void applyVorticity();

    // grid utils
    float div(int i, int j);
    float curl(int i, int j);
    float clamp(float n, float min, float max);
    float neighborhoodX(int i, int j);
    float neighborhoodY(int i, int j);
    float sample(float i, float j, int type);

    // misc helpers
    int idx(int i, int j) const { return j * gridX + i; }
};

#endif