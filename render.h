#ifndef RENDERER_H
#define RENDERER_H

#include <SDL2/SDL.h>
#include "irenderer.h"

class Renderer : public IRenderer {
public:
    Renderer(SDL_Window* window);
    ~Renderer();

    bool init() override;
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
    const float velScale = 0.05f;

    // draw utils
    void convertCoordinates(float simX, float simY, int& pixelX, int& pixelY);
    void mapValueToColor(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b);
    void mapValueToGreyscale(float value, float min, float max, Uint8& r, Uint8& g, Uint8& b);
    void drawFluidField(const ISimulator& simulator);
    void drawVelocityField(const ISimulator& simulator);
    void setPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b);
};

#endif