#ifndef RENDERER_H
#define RENDERER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include "irenderer.h"
#include "config.h"
#include "camera.h"

class Renderer : public IRenderer {
public:
    Renderer(SDL_Window* window, const Config& config);
    ~Renderer();

    bool init(const Config& config) override;
    void cleanup() override;
    void render(const ISimulator& simulator) override;

private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    Uint32* pixels;

    int windowWidth, windowHeight;
    float canvasScale;
    float simWidth, simHeight;

    // draw params
    int drawTarget; // 0=pressure, 1=smoke, 2=both
    bool drawVelocities;
    bool disableHistograms;
    float velScale;

    // histograms
    int frameCount;
    std::vector<int> densityHistogramBins;
    float densityHistogramMin, densityHistogramMax;
    std::vector<int> velocityHistogramBins;
    float velocityHistogramMin, velocityHistogramMax;

    CameraManager* cameraManager; // testing

    // draw utils
    void convertCoordinates(float simX, float simY, int& pixelX, int& pixelY);
    void mapValueToColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b);
    void mapValueToGreyscale(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b);
    void mapValueToVelocityColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b);
    void mapInkToColor(float r, float g, float b, Uint8& outR, Uint8& outG, Uint8& outB);
    void drawFluidField(const ISimulator& simulator);
    void drawVelocityField(const ISimulator& simulator);
    void computeHistograms(const ISimulator& simulator);
    void drawHistograms();
    void drawCameraFrame(); // testing
    void setPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b);

};

#endif