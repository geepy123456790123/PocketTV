#pragma once

#include <borealis/core/box.hpp>

class AuroraBackground : public brls::Box {
public:
    AuroraBackground();

    static brls::View* create();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;
};
