

#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_slider.hpp>
#include <borealis/views/cells/cell_input.hpp>

#include "utils/config_helper.hpp"
#include "utils/shader_helper.hpp"
#include "utils/number_helper.hpp"
#include "utils/activity_helper.hpp"

#include "fragment/player_setting.hpp"

#include "view/button_close.hpp"

#include "view/video_view.hpp"
#include "view/selector_cell.hpp"
#include "view/svg_image.hpp"
#include "view/mpv_core.hpp"

using namespace brls::literals;

PlayerSetting::PlayerSetting() {
    this->inflateFromXMLRes("xml/fragment/player_setting.xml");
    brls::Logger::debug("Fragment PlayerSetting: create");

    setupCommonSetting();

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    closebtn->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("Fragment PlayerSetting: delete"); }

brls::View* PlayerSetting::create() { return new PlayerSetting(); }

bool PlayerSetting::isTranslucent() { return true; }

brls::View* PlayerSetting::getDefaultFocus() { return this->settings->getDefaultFocus(); }

void PlayerSetting::setupCommonSetting() {
    auto locale = brls::Application::getLocale();

    btnMirror->init("tsvitch/player/setting/common/mirror"_i18n, MPVCore::VIDEO_MIRROR, [](bool value) {
        MPVCore::instance().setMirror(!MPVCore::VIDEO_MIRROR);
        GA("player_setting", {{"mirror", value ? "true" : "false"}});

        if (MPVCore::HARDWARE_DEC) {
            std::string hwdec = MPVCore::VIDEO_MIRROR ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD;
            MPVCore::instance().command_async("set", "hwdec", hwdec);
            brls::Logger::info("MPV hardware decode: {}", hwdec);
        }
    });

    btnSleep->setText("tsvitch/setting/app/playback/sleep"_i18n);
    updateCountdown(tsvitch::getUnixTime());
    btnSleep->registerClickAction([this](View* view) {
        std::vector<int> timeList           = {15, 30, 60, 90, 120};
        std::string min                     = "tsvitch/home/common/min"_i18n;
        std::vector<std::string> optionList = {"15 " + min, "30 " + min, "60 " + min, "90 " + min, "120 " + min};
        bool countdownStarted               = MPVCore::CLOSE_TIME != 0 && tsvitch::getUnixTime() < MPVCore::CLOSE_TIME;
        if (countdownStarted) {
            timeList.insert(timeList.begin(), -1);
            optionList.insert(optionList.begin(), "hints/off"_i18n);
        }
        BaseDropdown::text(
            "tsvitch/setting/app/playback/sleep"_i18n, optionList,
            [this, timeList, countdownStarted](int data) {
                if (countdownStarted && data == 0) {
                    MPVCore::CLOSE_TIME = 0;
                    GA("player_setting", {{"sleep", "-1"}});
                } else {
                    MPVCore::CLOSE_TIME = tsvitch::getUnixTime() + timeList[data] * 60;
                    GA("player_setting", {{"sleep", timeList[data]}});
                }
                updateCountdown(tsvitch::getUnixTime());
            },
            -1);
        return true;
    });

#ifdef ALLOW_FULLSCREEN
    auto& conf  = ProgramConfig::instance();
    btnFullscreen->init("tsvitch/setting/app/others/fullscreen"_i18n, conf.getBoolOption(SettingItem::FULLSCREEN),
                        [](bool value) {
                            ProgramConfig::instance().setSettingItem(SettingItem::FULLSCREEN, value);

                            VideoContext::FULLSCREEN = value;

                            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
                            GA("player_setting", {{"fullscreen", value ? "true" : "false"}});
                        });

    auto setOnTopCell = [this](bool enabled) {
        if (enabled) {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        } else {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        }
    };
    setOnTopCell(conf.getIntOptionIndex(SettingItem::ON_TOP_MODE) != 0);
    int onTopModeIndex = conf.getIntOption(SettingItem::ON_TOP_MODE);
    btnOnTopMode->setText("tsvitch/setting/app/others/always_on_top"_i18n);
    std::vector<std::string> onTopOptionList = {"hints/off"_i18n, "hints/on"_i18n,
                                                "tsvitch/player/setting/aspect/auto"_i18n};
    btnOnTopMode->setDetailText(onTopOptionList[onTopModeIndex]);
    btnOnTopMode->registerClickAction([this, onTopOptionList, setOnTopCell](brls::View* view) {
        BaseDropdown::text(
            "tsvitch/setting/app/others/always_on_top"_i18n, onTopOptionList,
            [this, onTopOptionList, setOnTopCell](int data) {
                btnOnTopMode->setDetailText(onTopOptionList[data]);
                ProgramConfig::instance().setSettingItem(SettingItem::ON_TOP_MODE, data);
                ProgramConfig::instance().checkOnTop();
                setOnTopCell(data != 0);
                GA("player_setting", {{"on_top_mode", data}});
            },
            ProgramConfig::instance().getIntOption(SettingItem::ON_TOP_MODE),
            "tsvitch/setting/app/others/always_on_top_hint"_i18n);
        return true;
    });

#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnOnTopMode->setVisibility(brls::Visibility::GONE);
#endif

    btnEqualizerReset->registerClickAction([this](View* view) {
        btnEqualizerBrightness->slider->setProgress(0.5f);
        btnEqualizerContrast->slider->setProgress(0.5f);
        btnEqualizerSaturation->slider->setProgress(0.5f);
        btnEqualizerGamma->slider->setProgress(0.5f);
        btnEqualizerHue->slider->setProgress(0.5f);
        return true;
    });
    registerHideBackground(btnEqualizerReset);

    setupEqualizerSetting(btnEqualizerBrightness, "tsvitch/player/setting/equalizer/brightness"_i18n,
                          SettingItem::PLAYER_BRIGHTNESS, MPVCore::instance().getBrightness());
    setupEqualizerSetting(btnEqualizerContrast, "tsvitch/player/setting/equalizer/contrast"_i18n,
                          SettingItem::PLAYER_CONTRAST, MPVCore::instance().getContrast());
    setupEqualizerSetting(btnEqualizerSaturation, "tsvitch/player/setting/equalizer/saturation"_i18n,
                          SettingItem::PLAYER_SATURATION, MPVCore::instance().getSaturation());
    setupEqualizerSetting(btnEqualizerGamma, "tsvitch/player/setting/equalizer/gamma"_i18n, SettingItem::PLAYER_GAMMA,
                          MPVCore::instance().getGamma());
    setupEqualizerSetting(btnEqualizerHue, "tsvitch/player/setting/equalizer/hue"_i18n, SettingItem::PLAYER_HUE,
                          MPVCore::instance().getHue());
}

void PlayerSetting::setupEqualizerSetting(brls::SliderCell* cell, const std::string& title, SettingItem item,
                                          int initValue) {
    if (initValue < -100) initValue = -100;
    if (initValue > 100) initValue = 100;
    cell->detail->setWidth(50);
    cell->title->setWidth(116);
    cell->title->setMarginRight(0);
    cell->slider->setStep(0.75f);
    cell->slider->setMarginRight(0);
    cell->slider->setPointerSize(20);
    cell->setDetailText(std::to_string(initValue));
    cell->init(title, (initValue + 100) * 0.005f, [cell, item](float value) {
        int data = (int)(value * 200 - 100);
        if (data < -100) data = -100;
        if (data > 100) data = 100;
        cell->detail->setText(std::to_string(data));
        switch (item) {
            case SettingItem::PLAYER_BRIGHTNESS:
                MPVCore::instance().setBrightness(data);
                break;
            case SettingItem::PLAYER_CONTRAST:
                MPVCore::instance().setContrast(data);
                break;
            case SettingItem::PLAYER_SATURATION:
                MPVCore::instance().setSaturation(data);
                break;
            case SettingItem::PLAYER_GAMMA:
                MPVCore::instance().setGamma(data);
                break;
            case SettingItem::PLAYER_HUE:
                MPVCore::instance().setHue(data);
                break;
            default:
                break;
        }
        static size_t iter = 0;
        brls::cancelDelay(iter);
        iter = brls::delay(200, []() {
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_BRIGHTNESS, MPVCore::VIDEO_BRIGHTNESS, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_CONTRAST, MPVCore::VIDEO_CONTRAST, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_SATURATION, MPVCore::VIDEO_SATURATION, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_GAMMA, MPVCore::VIDEO_GAMMA, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_HUE, MPVCore::VIDEO_HUE, false);
            ProgramConfig::instance().save();
        });
    });
    registerHideBackground(cell->getDefaultFocus());
}

void PlayerSetting::registerHideBackground(brls::View* view) {
    view->getFocusEvent()->subscribe([this](...) { this->setBackgroundColor(nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f)); });

    view->getFocusLostEvent()->subscribe(
        [this](...) { this->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop")); });
}

void PlayerSetting::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx) {
    static size_t updateTime = 0;
    size_t now               = tsvitch::getUnixTime();
    if (now != updateTime) {
        updateTime = now;
        updateCountdown(now);
    }
    Box::draw(vg, x, y, width, height, style, ctx);
}

void PlayerSetting::updateCountdown(size_t now) {
    if (MPVCore::CLOSE_TIME == 0 || now > MPVCore::CLOSE_TIME) {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        btnSleep->setDetailText("hints/off"_i18n);
    } else {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        btnSleep->setDetailText(tsvitch::sec2Time(MPVCore::CLOSE_TIME - now));
    }
}
