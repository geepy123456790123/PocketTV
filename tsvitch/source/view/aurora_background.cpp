#include "view/aurora_background.hpp"

AuroraBackground::AuroraBackground() { this->setFocusable(false); }

brls::View* AuroraBackground::create() { return new AuroraBackground(); }

void AuroraBackground::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                            brls::FrameContext* ctx) {
    auto fillRect = [&](NVGpaint paint) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    };

    nvgSave(vg);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(5, 6, 8));
    nvgFill(vg);

    fillRect(nvgRadialGradient(vg, x + width * 0.16f, y + height * 0.04f, 0.0f, width * 0.58f,
                               nvgRGBA(109, 213, 255, 28), nvgRGBA(109, 213, 255, 0)));
    fillRect(nvgRadialGradient(vg, x + width * 0.78f, y + height * 0.02f, 0.0f, width * 0.52f,
                               nvgRGBA(174, 121, 255, 25), nvgRGBA(174, 121, 255, 0)));
    fillRect(nvgRadialGradient(vg, x + width * 0.48f, y + height * 0.22f, 0.0f, width * 0.44f,
                               nvgRGBA(137, 255, 205, 18), nvgRGBA(137, 255, 205, 0)));

    nvgRestore(vg);
}
