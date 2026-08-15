#include "view/aurora_background.hpp"

#include <cmath>

AuroraBackground::AuroraBackground() { this->setFocusable(false); }

brls::View* AuroraBackground::create() { return new AuroraBackground(); }

void AuroraBackground::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                            brls::FrameContext* ctx) {
    float t = (brls::getCPUTimeUsec() / 1000000.0f) * 0.12f;

    auto fillRect = [&](NVGpaint paint) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    };

    auto driftX = [&](float base, float amount, float phase) { return base + std::sin(t + phase) * amount; };
    auto driftY = [&](float base, float amount, float phase) { return base + std::cos(t * 0.86f + phase) * amount; };

    nvgSave(vg);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(5, 6, 8));
    nvgFill(vg);

    fillRect(nvgRadialGradient(vg, x + width * driftX(0.16f, 0.035f, 0.0f),
                               y + height * driftY(0.04f, 0.035f, 1.2f), 0.0f, width * 0.58f,
                               nvgRGBA(109, 213, 255, 28), nvgRGBA(109, 213, 255, 0)));
    fillRect(nvgRadialGradient(vg, x + width * driftX(0.78f, 0.04f, 2.4f),
                               y + height * driftY(0.02f, 0.03f, 0.6f), 0.0f, width * 0.52f,
                               nvgRGBA(174, 121, 255, 25), nvgRGBA(174, 121, 255, 0)));
    fillRect(nvgRadialGradient(vg, x + width * driftX(0.48f, 0.03f, 4.1f),
                               y + height * driftY(0.22f, 0.04f, 2.0f), 0.0f, width * 0.44f,
                               nvgRGBA(137, 255, 205, 18), nvgRGBA(137, 255, 205, 0)));

    nvgRestore(vg);
    this->invalidate();
}
