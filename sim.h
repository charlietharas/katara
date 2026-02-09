#ifndef FLUID_SIMULATOR_H
#define FLUID_SIMULATOR_H

#include <vector>
#include "isimulator.h"
#include "config.h"

class Simulator : public ISimulator {
public:
    Simulator(const Config& config);
    ~Simulator();

    bool init(const Config& config, const ImageData* imageData = nullptr, float aspectRatio = 1.5f) override;
    void update() override;

    void moveCircle(int newGridX, int newGridY) override;

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
    void updateCircleAreas(int prevX, int prevY, int newX, int newY);

    // image initialization helpers
    void initializeFromImageData(const Config& config, const ImageData* imageData);

    // sim steps
    void integrate();
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