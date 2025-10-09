#ifndef FLUID_SIMULATOR_H
#define FLUID_SIMULATOR_H

#include <vector>
#include "isimulator.h"

class FluidSimulator : public ISimulator {
public:
    FluidSimulator();
    ~FluidSimulator();

    void init() override;
    void update() override;
    void reset() override;

    int getGridX() const override { return gridX; }
    int getGridY() const override { return gridY; }
    float getCellSize() const override { return cellHeight; }

    const std::vector<float>& getVelocityX() const override { return x; }
    const std::vector<float>& getVelocityY() const override { return y; }
    const std::vector<float>& getPressure() const override { return p; }
    const std::vector<float>& getDensity() const override { return d; }
    const std::vector<float>& getSolid() const override { return s; }

private:
    // grid params
    int resolution;
    int gridX, gridY;
    float domainHeight, domainWidth;
    float cellHeight, halfCellHeight;
    float xHeight, yHeight;

    // sim params
    float timeStep;
    float gravity;
    float density;
    float pressureMultiplier;
    float overrelaxationCoefficient;
    int gsIterations;
    bool doVorticity;
    float vorticity;
    float vorticityLen;
    float windTunnelVel;
    int pipeHeight;

    std::vector<float> x;        // x vel field
    std::vector<float> y;        // y vel field
    std::vector<float> s;        // solid field (1 = fluid, 0 = solid)
    std::vector<float> p;        // pressure field
    std::vector<float> d;        // density field

    // advection util arrays
    std::vector<float> newX, newY, newD;

    void setupBoundaries();
    void setupWindTunnel();
    void setupObstacles();

    // sim steps
    void integrate();
    void project();
    void extrapolate();
    void advect();
    void applyVorticity();
    void advectSmoke();

    // grid utils
    float div(int i, int j);
    float curl(int i, int j);
    float clamp(float n, float min, float max);
    float neighborhoodX(int i, int j);
    float neighborhoodY(int i, int j);
    float sample(float i, float j, int type);

    int idx(int i, int j) const { return j * gridX + i; }
};

#endif