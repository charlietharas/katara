#ifndef IRENDERER_H
#define IRENDERER_H

#include "isimulator.h"

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // render methods
    virtual bool init() = 0;
    virtual void cleanup() = 0;
    virtual void render(const ISimulator& simulator) = 0;
};

#endif