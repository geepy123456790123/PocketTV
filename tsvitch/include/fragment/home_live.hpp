

#pragma once

#include "view/auto_tab_frame.hpp"
#include "presenter/home_live.hpp"

#include <map>
#include <mutex>
#include <ctime>

typedef brls::Event<std::string> UpdateSearchEvent;

namespace brls {
class Label;
};
class RecyclingGrid;
class CustomButton;

class HomeLive : public AttachedView, public HomeLiveRequest {
public:
    HomeLive();

    void onLiveList(tsvitch::LiveM3u8ListResult result, bool firstLoad) override;

    ~HomeLive() override;

    void onCreate() override;

    void onError(const std::string &error) override;

    void onShow() override;

    brls::View* getDefaultFocus() override;

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    void search();

    void cancelSearch();

    void toggleFavorite();

    void downloadVideo();

    void openLiveSettings();

    void openPremiumInfo();

    void installForwarder();

    void selectGroupIndex(size_t index);

    void filter(const std::string &key);

    void setSearchCallback(UpdateSearchEvent *event);

    static View *create();

private:
    void showInitialSetup();
    void hideInitialSetup();
    void showChannels(tsvitch::LiveM3u8ListResult channels);
    brls::View* getFirstChannelFocus();
    brls::View* getSelectedGroupFocus();
    void pageGuide(int direction);
    void updateGuideHeader();
    void verifyPremiumSubscriptionAndLoad();

    int selectedGroupIndex = 0;
    std::string selectedGroupName = "All Channels";
    std::time_t guideStart = 0;
    bool isSearchActive    = false;
    bool isInitialLoadInProgress = false;
    bool guideLeftPressed = false;
    bool guideRightPressed = false;
    tsvitch::LiveM3u8ListResult channelsList;
    tsvitch::LiveM3u8ListResult visibleChannels;
    std::map<std::string, tsvitch::LiveM3u8ListResult> groupCache;
    std::mutex groupCacheMutex;
    std::shared_ptr<std::atomic<bool>> validityFlag;
    brls::Event<>::Subscription exitEventSubscription;
    bool hasExitSubscription = false;
    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/live/recyclingGrid");
    BRLS_BIND(RecyclingGrid, upRecyclingGrid, "dynamic/up/recyclingGrid");
    BRLS_BIND(brls::Label, statusLabel, "home/live/status");
    BRLS_BIND(brls::Label, guideHeader0, "home/live/header/0");
    BRLS_BIND(brls::Label, guideHeader1, "home/live/header/1");
    BRLS_BIND(brls::Label, guideHeader2, "home/live/header/2");
    BRLS_BIND(CustomButton, searchField, "home/search");
    BRLS_BIND(CustomButton, iptvSettingsButton, "home/iptv-settings");
    BRLS_BIND(brls::Box, setupPanel, "home/live/setup");
    BRLS_BIND(CustomButton, setupM3uButton, "home/live/setup/m3u");
    BRLS_BIND(CustomButton, setupPremiumButton, "home/live/setup/premium");
    BRLS_BIND(CustomButton, setupForwarderButton, "home/live/setup/forwarder");
};
