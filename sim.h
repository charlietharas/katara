#ifndef FLUID_SIMULATOR_H
#define FLUID_SIMULATOR_H

#include <vector>
#include "isimulator.h"

class FluidSimulator : public ISimulator {
public:
    FluidSimulator(int resolution = 150);
    ~FluidSimulator();

    void init(const ImageData* imageData = nullptr) override;
    void update() override;

    // mouse interaction methods
    void onMouseDown(int gridX, int gridY) override;
    void onMouseDrag(int gridX, int gridY) override;
    void onMouseUp() override;
    bool isInsideCircle(int i, int j);

    int getGridX() const override { return gridX; }
    int getGridY() const override { return gridY; }
    float getCellSize() const override { return cellHeight; }
    float getDomainWidth() const override { return domainWidth; }
    float getDomainHeight() const override { return domainHeight; }

  
    const std::vector<float>& getVelocityX() const override { return x; }
    const std::vector<float>& getVelocityY() const override { return y; }
    const std::vector<float>& getPressure() const override { return p; }
    const std::vector<float>& getDensity() const override { return d; }
    const std::vector<float>& getSolid() const override { return s; }
    const std::vector<float>& getRedInk() const override { return r_ink; }
    const std::vector<float>& getGreenInk() const override { return g_ink; }
    const std::vector<float>& getBlueInk() const override { return b_ink; }
    const std::vector<float>& getWaterContent() const override { return water; }
    bool isInkInitialized() const override { return inkInitialized; }

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

    // wind tunnel state
    float windTunnelStart; // 0-1 (pass this one in)
    float windTunnelEnd;
    int windTunnelStartCell; // reference; calculated in init
    int windTunnelEndCell;
    int pipeHeight;
    int windTunnelSide; // 0, 1, 2, 3 = left, top, bottom, right; -1 = disabled
    float windTunnelVelocity; // magnitude; direction inferred

    // momentum transfer parameters
    float momentumTransferCoeff;
    float momentumTransferRadius;

    std::vector<float> x; // x vel field
    std::vector<float> y; // y vel field
    std::vector<float> s; // solid field (1 = fluid, 0 = solid)
    std::vector<float> p; // pressure field
    std::vector<float> d; // density field

    // advection util arrays
    std::vector<float> newX, newY, newD;

    // ink diffusion
    std::vector<float> r_ink, g_ink, b_ink;
    std::vector<float> water;
    std::vector<float> r_ink_prev, g_ink_prev, b_ink_prev;
    float mixingRate;
    float diffusionRate;
    float pressureStrength;
    float temporalWeightCurrent;
    bool inkInitialized;

    // configuration
    bool domainSetByImage;

    // circle state
    int circleX, circleY;
    int prevCircleX, prevCircleY;
    float circleVelX, circleVelY;
    int circleRadius;
    bool isDragging;

    // circle movement
    void setupCircle();
    void moveCircle(int newGridX, int newGridY);
    void updateCircle(int prevX, int prevY, int newX, int newY);
    void enforceBoundaryConditions();
    void circleMomentumTransfer();
    void setupEdges();
    void updateCircleAreas(int prevX, int prevY, int newX, int newY);

    // sim steps
    void integrate();
    void project();
    void extrapolate();
    void advect();
    void applyVorticity();
    void smokeAdvect();

    // ink diffusion methods
    void inkUpdate();
    void inkAdvection();
    void inkDiffusion();
    void inkWaterMix();
    void inkTemporalBlend();

    // grid utils
    float div(int i, int j);
    float curl(int i, int j);
    float clamp(float n, float min, float max);
    float neighborhoodX(int i, int j);
    float neighborhoodY(int i, int j);
    float sample(float i, float j, int type);

    // image initialization helpers
    void initializeFromImageData(const ImageData* imageData);

    // misc helpers
    bool shouldSkipInkCell(int i, int j, bool checkNoInk = true) const;
    int idx(int i, int j) const { return j * gridX + i; }
};

#endif