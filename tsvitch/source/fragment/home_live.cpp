#include <utility>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/tab_frame.hpp>

#include "fragment/home_live.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/grid_dropdown.hpp"
#include "utils/image_helper.hpp"
#include "utils/activity_helper.hpp"
#include "view/custom_button.hpp"

#include "core/HistoryManager.hpp"
#include "core/FavoriteManager.hpp"
#include "core/ChannelManager.hpp"
#include "core/DownloadManager.hpp"
#include "utils/stream_helper.hpp"
#include "core/DownloadProgressManager.hpp"
#include "core/EpgManager.hpp"

#include "utils/config_helper.hpp"

#ifdef BUILTIN_NSP
#include "nspmini.hpp"
#endif

using namespace brls::literals;

namespace {

std::string formatGuideTime(std::time_t value) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M");
    return out.str();
}

std::string formatGuideHeaderTime(std::time_t value) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;
    return fmt::format("{}:{:02d}{}", hour, local.tm_min, local.tm_hour < 12 ? "am" : "pm");
}

std::string programmeText(const tsvitch::EpgProgramme& programme, std::time_t slotStart, std::time_t slotStop) {
    if (programme.title.empty()) return "Live TV";
    return fmt::format("{}\n{}-{}", programme.title, formatGuideTime(std::max(programme.start, slotStart)),
                       formatGuideTime(std::min(programme.stop, slotStop)));
}

constexpr float GUIDE_SLOT_WIDTH = 247.0f;
constexpr float GUIDE_SLOT_GAP   = 6.0f;
constexpr int GUIDE_SLOT_SECONDS = 30 * 60;
constexpr const char* PREMIUM_REDEEM_URL = "https://api.pocket-tv.net/api/redeem-code";

std::string normalizedChannelTitle(const tsvitch::LiveM3u8& channel) {
    std::string title = channel.title.empty() ? channel.chno : channel.title;
    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return title;
}

void sortChannelsAlphabetically(tsvitch::LiveM3u8ListResult& channels) {
    std::sort(channels.begin(), channels.end(), [](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) {
        auto titleA = normalizedChannelTitle(a);
        auto titleB = normalizedChannelTitle(b);
        if (titleA == titleB) return a.url < b.url;
        return titleA < titleB;
    });
}

std::string normalizeActivationCode(std::string code) {
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    code.erase(std::remove_if(code.begin(), code.end(), [](unsigned char c) {
        return !std::isalnum(c);
    }), code.end());
    return code;
}

}  // namespace

class DynamicGroupChannels : public RecyclingGridItem {
public:
    explicit DynamicGroupChannels(const std::string& xml) {
        this->inflateFromXMLRes(xml);
        fontColor     = brls::Application::getTheme().getColor("brls/text");
        selectedColor = nvgRGB(125, 220, 255);
    }

    void setTitle(const std::string& title) { this->labelTitle->setText(title); }

    void setSelected(bool selected) { this->labelTitle->setTextColor(primary ? fontColor : (selected ? selectedColor : fontColor)); }

    void setPrimary(bool primary) {
        this->primary = primary;
        this->labelTitle->setFontSize(primary ? 21 : 17);
    }

    void prepareForReuse() override {
        this->primary = false;
        this->labelTitle->setText("");
        this->labelTitle->setTextColor(fontColor);
    }

    void cacheForReuse() override {}

    static RecyclingGridItem* create(const std::string& xml = "xml/views/group_channel_dynamic.xml") {
        return new DynamicGroupChannels(xml);
    }

private:
    BRLS_BIND(brls::Label, labelTitle, "title");
    NVGcolor selectedColor{};
    NVGcolor fontColor{};
    bool primary = false;
};

class DataSourceUpList : public RecyclingGridDataSource {
public:
    using OnGroupSelected = std::function<void(const std::string&)>;
    explicit DataSourceUpList(std::vector<std::string> result, OnGroupSelected cb = nullptr)
        : list(std::move(result)), onGroupSelected(cb) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        DynamicGroupChannels* item = (DynamicGroupChannels*)recycler->dequeueReusableCell("Cell");
        item->setTitle(this->list[index]);
        item->setPrimary(this->list[index] == "Favorites" || this->list[index] == "All Channels");
        item->setSelected(index == selectedIndex);  // Imposta sempre la selezione!
        return item;
    }

    size_t getItemCount() override { return list.size(); }

    void setSelectedIndex(RecyclingGrid* recycler, size_t index) {
        brls::Logger::debug("setSelectedIndex: {}", index);
        if (index >= list.size()) return;
        selectedIndex = index;
        std::vector<RecyclingGridItem*>& items = recycler->getGridItems();
        for (auto& i : items) {
            auto* cell = dynamic_cast<DynamicGroupChannels*>(i);
            if (cell) cell->setSelected(false);
        }
        auto* item    = dynamic_cast<DynamicGroupChannels*>(recycler->getGridItemByIndex(index));
        if (!item) return;
        item->setSelected(true);

        // Salva l'indice selezionato
        ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, static_cast<int>(index));

        if (onGroupSelected) onGroupSelected(list[index]);
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        brls::Logger::debug("onItemSelected: {}", index);
        std::vector<RecyclingGridItem*>& items = recycler->getGridItems();
        for (auto& i : items) {
            auto* cell = dynamic_cast<DynamicGroupChannels*>(i);
            if (cell) cell->setSelected(false);
        }

        selectedIndex = index;

        auto* item = dynamic_cast<DynamicGroupChannels*>(recycler->getGridItemByIndex(index));
        if (!item) return;
        item->setSelected(true);

        // Salva l'indice selezionato
        ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, static_cast<int>(index));

        if (onGroupSelected) onGroupSelected(list[index]);
    }

    void appendData(const std::vector<std::string>& data) {
        this->list.insert(this->list.end(), data.begin(), data.end());
    }

    void clearData() override { this->list.clear(); }

    const std::string& getGroupNameByIndex(size_t index) const {
        static std::string empty;
        if (index < list.size()) return list[index];
        return empty;
    }

private:
    std::vector<std::string> list;
    size_t selectedIndex = -1;
    OnGroupSelected onGroupSelected;
};

const std::string GridMainAreaCellContentXML = R"xml(
<brls:Box
        width="auto"
        height="@style/brls/sidebar/item_height"
        focusable="true"
        paddingTop="12.5"
        paddingBottom="12.5"
        alignItems="center">

    <brls:Image
        id="area/avatar"
        scalingType="fill"
        cornerRadius="4"
        marginLeft="10"
        marginRight="10"
        width="40"
        height="40"/>

    <brls:Label
            id="area/title"
            width="auto"
            height="auto"
            grow="1"
            fontSize="22" />

</brls:Box>
)xml";

class GridMainAreaCell : public RecyclingGridItem {
public:
    GridMainAreaCell() { this->inflateFromXMLString(GridMainAreaCellContentXML); }

    void setData(const std::string& name, const std::string& pic) {
        this->title->setText(name);
        this->title->setTextColor(fontColor);

        if (pic.empty()) {
            this->image->setImageFromRes("pictures/22_open.png");
        } else {
            ImageHelper::with(image)->load(pic + ImageHelper::face_ext);
        }
    }

    void setSelected(bool value) { this->title->setTextColor(value ? selectedColor : fontColor); }

    void prepareForReuse() override {
        this->image->setImageFromRes("pictures/video-card-bg.png");
        this->title->setText("");
        this->title->setTextColor(fontColor);
    }

    void cacheForReuse() override { ImageHelper::clear(this->image); }

    static RecyclingGridItem* create() { return new GridMainAreaCell(); }

protected:
    BRLS_BIND(brls::Label, title, "area/title");
    BRLS_BIND(brls::Image, image, "area/avatar");

    NVGcolor selectedColor = brls::Application::getTheme().getColor("color/tsvitch");
    NVGcolor fontColor     = brls::Application::getTheme().getColor("brls/text");
};

const std::string LiveGuideRowCellXML = R"xml(
<brls:Box
        width="auto"
        height="66"
        focusable="true"
        paddingLeft="8"
        paddingRight="8"
        paddingTop="2"
        paddingBottom="2"
        alignItems="center"
        highlightCornerRadius="12"
        cornerRadius="8">

    <brls:Box width="220" height="54" axis="row" alignItems="center">
        <brls:Image
                id="guide/logo"
                scalingType="fit"
                marginRight="10"
                width="34"
                height="31"/>
        <brls:Label
                id="guide/logo/fallback"
                positionType="absolute"
                positionLeft="8"
                positionTop="16"
                width="34"
                height="14"
                fontSize="7"
                textColor="#F8FAFC"
                horizontalAlign="center"
                singleLine="true"
                text=""/>
        <brls:Label
                id="guide/heart"
                positionType="absolute"
                positionLeft="28"
                positionTop="5"
                width="12"
                height="12"
                fontSize="9"
                textColor="#FF375F"
                text=""/>
        <brls:Box width="164" height="auto" axis="column">
            <brls:Label
                    id="guide/channel"
                    width="164"
                    height="auto"
                    fontSize="16"
                    textColor="#F8FAFC"
                    singleLine="true"/>
            <brls:Label
                    id="guide/group"
                    width="164"
                    height="auto"
                    fontSize="13"
                    textColor="#A7B0BA"
                    singleLine="true"/>
        </brls:Box>
    </brls:Box>

    <brls:Box id="guide/slot0/box" width="247" height="54" marginLeft="6" paddingLeft="10" paddingRight="10" paddingTop="7" paddingBottom="7" backgroundColor="#101218" cornerRadius="8">
        <brls:Label id="guide/slot0" width="100%" height="auto" fontSize="16" textColor="#F8FAFC"/>
    </brls:Box>
    <brls:Box id="guide/slot1/box" width="247" height="54" marginLeft="6" paddingLeft="10" paddingRight="10" paddingTop="7" paddingBottom="7" backgroundColor="#101218" cornerRadius="8">
        <brls:Label id="guide/slot1" width="100%" height="auto" fontSize="16" textColor="#F8FAFC"/>
    </brls:Box>
    <brls:Box id="guide/slot2/box" width="247" height="54" marginLeft="6" paddingLeft="10" paddingRight="10" paddingTop="7" paddingBottom="7" backgroundColor="#101218" cornerRadius="8">
        <brls:Label id="guide/slot2" width="100%" height="auto" fontSize="16" textColor="#F8FAFC"/>
    </brls:Box>
</brls:Box>
)xml";

const std::string LiveGuideSectionCellXML = R"xml(
<brls:Box
        width="auto"
        height="42"
        focusable="false"
        paddingLeft="12"
        paddingRight="12"
        alignItems="center">
    <brls:Label
            id="guide/section/title"
            width="auto"
            height="auto"
            fontSize="24"
            textColor="#F8FAFC"
            singleLine="true"/>
</brls:Box>
)xml";

class LiveGuideRowCell : public RecyclingGridItem {
public:
    LiveGuideRowCell() { this->inflateFromXMLString(LiveGuideRowCellXML); }

    void setChannel(const tsvitch::LiveM3u8& channel, std::time_t guideStart) {
        this->channel = channel;
        this->heartLabel->setText(FavoriteManager::get()->isFavorite(channel.url) ? "♥" : "");
        this->channelLabel->setText(channel.title);
        this->groupLabel->setText(channel.groupTitle.empty() ? "Live TV" : channel.groupTitle);

        std::string logoUrl = channel.logo.empty() ? tsvitch::EpgManager::instance().channelIcon(channel.id) : channel.logo;
        if (logoUrl.empty()) {
            this->logo->setImageFromRes("pictures/empty.png");
            this->logoFallbackLabel->setText(this->fallbackLogoText(channel));
        } else {
            this->logoFallbackLabel->setText("");
            ImageHelper::with(logo)->loadWithDiskCache(logoUrl);
        }

        this->renderProgrammeSlots(guideStart);
    }

    void prepareForReuse() override {
        this->channelLabel->setText("");
        this->groupLabel->setText("");
        this->heartLabel->setText("");
        this->logo->setImageFromRes("pictures/empty.png");
        this->logoFallbackLabel->setText("");
        this->resetProgrammeSlots();
    }

    void cacheForReuse() override { ImageHelper::clear(this->logo); }

    tsvitch::LiveM3u8 getChannel() const { return channel; }

    static RecyclingGridItem* create() { return new LiveGuideRowCell(); }

private:
    void resetProgrammeSlots() {
        brls::Box* slotBoxes[] = {slot0Box, slot1Box, slot2Box};
        brls::Label* slots[]   = {slot0, slot1, slot2};
        for (size_t i = 0; i < 3; ++i) {
            slotBoxes[i]->setVisibility(brls::Visibility::VISIBLE);
            slotBoxes[i]->setWidth(GUIDE_SLOT_WIDTH);
            slots[i]->setText("");
        }
    }

    void renderProgrammeSlots(std::time_t guideStart) {
        this->resetProgrammeSlots();

        brls::Box* slotBoxes[] = {slot0Box, slot1Box, slot2Box};
        brls::Label* slots[]   = {slot0, slot1, slot2};
        auto windowStop      = guideStart + (3 * GUIDE_SLOT_SECONDS);
        int slot             = 0;

        while (slot < 3) {
            auto slotStart  = guideStart + (slot * GUIDE_SLOT_SECONDS);
            auto slotStop   = slotStart + GUIDE_SLOT_SECONDS;
            auto programmes = tsvitch::EpgManager::instance().window(channel.id, slotStart, slotStop);

            if (programmes.empty()) {
                slots[slot]->setText(fmt::format("{}\nLive TV", formatGuideTime(slotStart)));
                slot++;
                continue;
            }

            const auto& programme = programmes.front();
            auto visibleStop      = std::min(programme.stop, windowStop);
            int endSlot           = static_cast<int>((visibleStop - guideStart + GUIDE_SLOT_SECONDS - 1) / GUIDE_SLOT_SECONDS);
            endSlot               = std::max(slot + 1, std::min(3, endSlot));
            int span              = endSlot - slot;

            slotBoxes[slot]->setWidth((GUIDE_SLOT_WIDTH * span) + (GUIDE_SLOT_GAP * (span - 1)));
            slots[slot]->setText(programmeText(programme, guideStart, windowStop));
            for (int hidden = slot + 1; hidden < endSlot; ++hidden) {
                slotBoxes[hidden]->setVisibility(brls::Visibility::GONE);
            }
            slot = endSlot;
        }
    }

    std::string fallbackLogoText(const tsvitch::LiveM3u8& channel) {
        std::string source = channel.chno.empty() ? channel.title : channel.chno;
        std::string out;
        for (char c : source) {
            if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            if (out.size() >= 4) break;
        }
        return out.empty() ? "TV" : out;
    }

    tsvitch::LiveM3u8 channel;
    BRLS_BIND(brls::Image, logo, "guide/logo");
    BRLS_BIND(brls::Label, logoFallbackLabel, "guide/logo/fallback");
    BRLS_BIND(brls::Label, heartLabel, "guide/heart");
    BRLS_BIND(brls::Label, channelLabel, "guide/channel");
    BRLS_BIND(brls::Label, groupLabel, "guide/group");
    BRLS_BIND(brls::Box, slot0Box, "guide/slot0/box");
    BRLS_BIND(brls::Box, slot1Box, "guide/slot1/box");
    BRLS_BIND(brls::Box, slot2Box, "guide/slot2/box");
    BRLS_BIND(brls::Label, slot0, "guide/slot0");
    BRLS_BIND(brls::Label, slot1, "guide/slot1");
    BRLS_BIND(brls::Label, slot2, "guide/slot2");
};

class LiveGuideSectionCell : public RecyclingGridItem {
public:
    LiveGuideSectionCell() { this->inflateFromXMLString(LiveGuideSectionCellXML); }

    void setTitle(const std::string& title) { this->title->setText(title); }

    void prepareForReuse() override { this->title->setText(""); }

    void cacheForReuse() override {}

    static RecyclingGridItem* create() { return new LiveGuideSectionCell(); }

private:
    BRLS_BIND(brls::Label, title, "guide/section/title");
};

struct LiveGuideDisplayRow {
    bool section = false;
    std::string title;
    tsvitch::LiveM3u8 channel;
};

class DataSourceLiveVideoList : public RecyclingGridDataSource {
public:
    explicit DataSourceLiveVideoList(const tsvitch::LiveM3u8ListResult& result, std::time_t guideStart = 0)
        : videoList(result), guideStart(guideStart) {
        this->buildRows();
    }
    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto& row = this->displayRows[index];
        if (row.section) {
            auto* item = (LiveGuideSectionCell*)recycler->dequeueReusableCell("Section");
            item->setTitle(row.title);
            return item;
        }
        auto* item = (LiveGuideRowCell*)recycler->dequeueReusableCell("Cell");
        item->setChannel(row.channel, this->guideStart);
        return item;
    }

    size_t getItemCount() override { return displayRows.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        if (index >= displayRows.size() || displayRows[index].section) return;
        HistoryManager::get()->add(displayRows[index].channel);
        Intent::openLive(playList, rowToPlayIndex[index], [recycler]() { recycler->reloadData(); });
    }

    void appendData(const tsvitch::LiveM3u8ListResult& data) {
        this->videoList.insert(this->videoList.end(), data.begin(), data.end());
    }

    void clearData() override { this->videoList.clear(); }

private:
    void buildRows() {
        tsvitch::LiveM3u8ListResult favorites;
        tsvitch::LiveM3u8ListResult allChannels;

        for (const auto& channel : videoList) {
            if (FavoriteManager::get()->isFavorite(channel.url)) favorites.push_back(channel);
            allChannels.push_back(channel);
        }
        sortChannelsAlphabetically(favorites);
        sortChannelsAlphabetically(allChannels);

        if (!favorites.empty()) {
            displayRows.push_back({true, "Favorites", {}});
            for (const auto& channel : favorites) addChannelRow(channel);
        }

        displayRows.push_back({true, "All Channels", {}});
        for (const auto& channel : allChannels) addChannelRow(channel);
    }

    void addChannelRow(const tsvitch::LiveM3u8& channel) {
        size_t rowIndex = displayRows.size();
        displayRows.push_back({false, "", channel});
        rowToPlayIndex[rowIndex] = playList.size();
        playList.push_back(channel);
    }

    tsvitch::LiveM3u8ListResult videoList;
    tsvitch::LiveM3u8ListResult playList;
    std::vector<LiveGuideDisplayRow> displayRows;
    std::unordered_map<size_t, size_t> rowToPlayIndex;
    std::time_t guideStart = 0;
};

HomeLive::HomeLive() {
    this->inflateFromXMLRes("xml/fragment/home_live.xml");
    brls::Logger::info("Fragment HomeLive: constructor called");
    
    // Inizializza il flag di validità
    validityFlag = std::make_shared<std::atomic<bool>>(true);
    
    // Sottoscrivi all'evento di uscita per cancellare tutti i task asincroni
    exitEventSubscription = brls::Application::getExitEvent()->subscribe([this]() {
        brls::Logger::info("HomeLive: Exit event received, canceling all async operations");
        if (validityFlag) {
            validityFlag->store(false);
        }
    });
    hasExitSubscription = true;
    
    recyclingGrid->registerCell("Cell", []() { return LiveGuideRowCell::create(); });
    recyclingGrid->registerCell("Section", []() { return LiveGuideSectionCell::create(); });

    upRecyclingGrid->registerCell("Cell", []() { return DynamicGroupChannels::create(); });
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);

    this->registerAction("M3U / EPG", brls::BUTTON_START, [this](...) {
        this->openLiveSettings();
        return true;
    });

    if (this->setupM3uButton) {
        this->setupM3uButton->registerClickAction([this](...) -> bool {
            this->openLiveSettings();
            return true;
        });
    }
    if (this->setupPremiumButton) {
        this->setupPremiumButton->registerClickAction([this](...) -> bool {
            ProgramConfig::instance().setSettingItem(SettingItem::IPTV_MODE, 2);
            this->openLiveSettings();
            return true;
        });
    }
    if (this->setupForwarderButton) {
        this->setupForwarderButton->registerClickAction([this](...) -> bool {
            this->installForwarder();
            return true;
        });
    }

    auto now = std::time(nullptr);
    guideStart = now - (now % (30 * 60));
    tsvitch::EpgManager::instance().loadFromUrl(ProgramConfig::instance().getEpgUrl(), [this]() {
        if (this->recyclingGrid) this->recyclingGrid->reloadData();
    });

    // Sottoscrivi all'evento di cambio M3U8
    OnM3U8UrlChanged.subscribe([this]() {
        brls::Logger::debug("OnM3U8UrlChanged: showing skeleton and requesting channel list");
        // Mostra lo skeleton per indicare che stiamo caricando
        brls::Threading::sync([this]() {
            hideInitialSetup();
            statusLabel->setText("Refreshing PocketTV guide...");
            recyclingGrid->showSkeleton();
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        });
        
        ChannelManager::get()->remove();
        this->requestLiveList();
        //reset index group
        this->selectGroupIndex(0);
    });

    // Sottoscrivi all'evento di cambio modalità IPTV
    OnIPTVModeChanged.subscribe([this]() {
        brls::Logger::debug("OnIPTVModeChanged: showing skeleton and requesting channel list");
        // Mostra lo skeleton per indicare che stiamo caricando
        brls::Threading::sync([this]() {
            hideInitialSetup();
            statusLabel->setText("Loading PocketTV channels...");
            recyclingGrid->showSkeleton();
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        });
        
        ChannelManager::get()->remove();
        if (ProgramConfig::instance().getIntOption(SettingItem::IPTV_MODE) == 2) {
            this->verifyPremiumSubscriptionAndLoad();
        } else {
            this->requestLiveList();
        }
        //reset index group
        this->selectGroupIndex(0);
    });

    OnPremiumChanged.subscribe([this]() {
        brls::Logger::debug("OnPremiumChanged: verifying premium subscription");
        if (ProgramConfig::instance().getIntOption(SettingItem::IPTV_MODE) != 2) return;
        brls::Threading::sync([this]() {
            hideInitialSetup();
            statusLabel->setText("Verifying PocketTV Premium...");
            recyclingGrid->showSkeleton();
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        });
        ChannelManager::get()->remove();
        this->verifyPremiumSubscriptionAndLoad();
        this->selectGroupIndex(0);
    });

    // Sottoscrivi all'evento di cambio Xtream
    OnXtreamChanged.subscribe([this](const XtreamData& xtreamData) {
        brls::Logger::debug("OnXtreamChanged: url={}, username={}, showing skeleton and requesting channel list", 
                           xtreamData.url, xtreamData.username);
        // Mostra lo skeleton per indicare che stiamo caricando
        brls::Threading::sync([this]() {
            hideInitialSetup();
            statusLabel->setText("Loading Xtream channels...");
            recyclingGrid->showSkeleton();
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        });
        
        ChannelManager::get()->remove();
        this->requestLiveList();
        //reset index group
        this->selectGroupIndex(0);
    });
    
    // Mostra sempre lo skeleton all'inizio per UI non-bloccante
    brls::Logger::debug("HomeLive constructor: Showing skeleton for non-blocking UI");
    statusLabel->setText("Loading PocketTV...");
    recyclingGrid->showSkeleton();
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    
    // Imposta il flag per indicare che il caricamento è in corso
    isInitialLoadInProgress = true;
    
    // Check if we're in Xtream mode and load channels immediately
    int iptvMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0);
    brls::Logger::info("HomeLive constructor: IPTV mode is {}", iptvMode);
    
    if (iptvMode == 2) {
        brls::Logger::info("HomeLive constructor: Premium mode detected, verifying subscription");
        this->verifyPremiumSubscriptionAndLoad();
        isInitialLoadInProgress = false;
    } else if (iptvMode == 1) {
        brls::Logger::info("HomeLive constructor: Xtream mode detected, using smart cache approach");
        
        // Prova prima la cache intelligente anche per Xtream
        brls::Threading::async([this, validityFlag = this->validityFlag] {
            // Controlla se l'app è ancora valida prima di procedere
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive: Xtream async task canceled - app exiting");
                return;
            }
            
            auto cachedChannels = ChannelManager::get()->loadIfValid(); 
            
            brls::sync([this, cachedChannels, validityFlag]() {
                // Controlla di nuovo la validità prima di aggiornare l'UI
                if (!validityFlag || !validityFlag->load()) {
                    brls::Logger::debug("HomeLive: Xtream sync task canceled - app exiting");
                    return;
                }
                
                if (!cachedChannels.empty()) {
                    brls::Logger::info("HomeLive: Using valid Xtream cache with {} channels", cachedChannels.size());
                    statusLabel->setText(fmt::format("Loaded {} cached channels", cachedChannels.size()));
                    this->onLiveList(cachedChannels, false);
                } else {
                    brls::Logger::info("HomeLive: Xtream cache invalid/empty, requesting fresh data");
                    statusLabel->setText("Downloading Xtream channels...");
                    this->requestLiveList();
                }
                isInitialLoadInProgress = false; // Reset flag quando completato
            });
        });
    } else {
        brls::Logger::debug("HomeLive constructor: M3U8 mode detected, will use intelligent caching");
        
        // Per M3U8 mode, usa cache intelligente con timeout più lungo
        brls::Threading::async([this, validityFlag = this->validityFlag] {
            // Controlla se l'app è ancora valida prima di procedere
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive: M3U8 async task canceled - app exiting");
                return;
            }
            
            brls::Logger::debug("HomeLive: Starting smart cache check in background thread");
            
            // Cache più lunga per M3U8 (1 mese) perché cambia meno frequentemente
            auto cachedChannels = ChannelManager::get()->loadIfValid();
            brls::Logger::info("HomeLive: Smart cache check completed, found {} channels", cachedChannels.size());
            
            brls::sync([this, cachedChannels, validityFlag]() {
                // Controlla di nuovo la validità prima di aggiornare l'UI
                if (!validityFlag || !validityFlag->load()) {
                    brls::Logger::debug("HomeLive: M3U8 sync task canceled - app exiting");
                    return;
                }
                
                if (!cachedChannels.empty()) {
                    brls::Logger::info("HomeLive constructor: Using valid M3U8 cache ({} channels found)", cachedChannels.size());
                    statusLabel->setText(fmt::format("Loaded {} cached channels", cachedChannels.size()));
                    this->onLiveList(cachedChannels, false);
                } else if (ProgramConfig::instance().getM3U8Url().empty()) {
                    brls::Logger::info("HomeLive constructor: no M3U8 URL configured");
                    this->showInitialSetup();
                } else {
                    brls::Logger::info("HomeLive constructor: M3U8 cache is invalid or empty, requesting fresh channels");
                    statusLabel->setText("Downloading M3U playlist...");
                    this->requestLiveList();
                }
                isInitialLoadInProgress = false; // Reset flag quando completato
            });
        });
    }
}

void HomeLive::onError(const std::string& error) {
    brls::Logger::error("Fragment HomeLive: onError: {}", error);
    brls::sync([this, error]() {
        this->statusLabel->setText(error);
        this->recyclingGrid->setError(error);
        this->upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    });

    //dialog to show error
    auto dialog = new brls::Dialog("hints/network_error"_i18n);
    dialog->addButton("hints/back"_i18n, []() {});
    dialog->open();
}

void HomeLive::onLiveList(tsvitch::LiveM3u8ListResult result, bool firstLoad) {
    brls::Logger::info("Fragment HomeLive: onLiveList - received {} channels", result.size());
    statusLabel->setText(fmt::format("Preparing {} channels", result.size()));
    if (result.empty()) {
        statusLabel->setText("No channels found in playlist");
        recyclingGrid->setEmpty();
        upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        hideInitialSetup();
        return;
    }

    this->hideInitialSetup();
    this->channelsList = std::move(result);
    sortChannelsAlphabetically(this->channelsList);
    this->updateGuideHeader();

    std::unordered_map<std::string, std::vector<size_t>> groupIndices;
    groupIndices.reserve(100);
    for (size_t i = 0; i < this->channelsList.size(); ++i) {
        auto groupTitle = this->channelsList[i].groupTitle.empty() ? std::string("Uncategorized") : this->channelsList[i].groupTitle;
        groupIndices[groupTitle].push_back(i);
    }

    std::vector<std::string> groupTitles;
    groupTitles.reserve(groupIndices.size() + 2);
    groupTitles.push_back("Favorites");
    groupTitles.push_back("All Channels");
    for (const auto& pair : groupIndices) {
        groupTitles.push_back(pair.first);
    }
    std::sort(groupTitles.begin() + 2, groupTitles.end());

    {
        std::lock_guard<std::mutex> lock(groupCacheMutex);
        groupCache.clear();
        auto favorites = FavoriteManager::get()->getFavorites();
        sortChannelsAlphabetically(favorites);
        groupCache["All Channels"] = this->channelsList;
        groupCache["Favorites"]    = std::move(favorites);
        for (const auto& pair : groupIndices) {
            tsvitch::LiveM3u8ListResult filtered;
            filtered.reserve(pair.second.size());
            for (size_t idx : pair.second) {
                filtered.push_back(this->channelsList[idx]);
            }
            sortChannelsAlphabetically(filtered);
            groupCache[pair.first] = std::move(filtered);
        }
    }

    upRecyclingGrid->setVisibility(brls::Visibility::VISIBLE);
    auto* upList = new DataSourceUpList(groupTitles, [this](const std::string& group) {
        tsvitch::LiveM3u8ListResult filtered;
        {
            std::lock_guard<std::mutex> lock(groupCacheMutex);
            if (group == "Favorites") {
                auto favorites = FavoriteManager::get()->getFavorites();
                sortChannelsAlphabetically(favorites);
                groupCache["Favorites"] = std::move(favorites);
            }
            if (groupCache.count(group)) filtered = groupCache[group];
        }
        if (filtered.empty()) {
            this->visibleChannels.clear();
            this->recyclingGrid->setEmpty(group == "Favorites" ? "No favorite channels yet" : "No channels in this group");
            return;
        }
        this->showChannels(std::move(filtered));
    });
    upRecyclingGrid->setDataSource(upList);
    this->selectedGroupIndex = 1;
    this->selectGroupIndex(1);
    upRecyclingGrid->setDefaultCellFocus(1);
    if (auto* focus = upRecyclingGrid->getDefaultFocus()) {
        brls::Application::giveFocus(focus);
    }
    brls::delay(10, [this]() {
        if (this->upRecyclingGrid && this->upRecyclingGrid->getVisibility() == brls::Visibility::VISIBLE) {
            this->upRecyclingGrid->setDefaultCellFocus(this->selectedGroupIndex);
            brls::Application::giveFocus(this->upRecyclingGrid->getDefaultFocus());
        }
    });
    statusLabel->setText(fmt::format("{} channels - logos load as available", this->channelsList.size()));

    if (firstLoad) {
        auto toSave = this->channelsList;
        brls::Threading::async([data = std::move(toSave)]() {
            try {
                ChannelManager::get()->saveWithTimestamp(data);
            } catch (const std::exception& e) {
                brls::Logger::error("HomeLive: Exception in async saveWithTimestamp: {}", e.what());
            } catch (...) {
                brls::Logger::error("HomeLive: Unknown exception in async saveWithTimestamp");
            }
        });
    }
    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        if (isSearchActive) {
            this->cancelSearch();
        } else {
            auto dialog = new brls::Dialog("hints/exit_hint"_i18n);
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->addButton("hints/ok"_i18n, []() { brls::Application::quit(); });
            dialog->open();
        }
        return true;
    });

    this->registerAction("hints/search"_i18n, brls::BUTTON_Y, [this](...) {
        this->search();
        return true;
    });

    if (this->iptvSettingsButton) {
        this->iptvSettingsButton->registerClickAction([this](brls::View* view) -> bool {
            this->openLiveSettings();
            return true;
        });
    }

    this->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](...) {
        this->toggleFavorite();
        return true;
    });

    this->registerAction("Scarica video", brls::BUTTON_RT, [this](...) {
        this->downloadVideo();
        return true;
    });

    this->registerAction("Guide -30m", brls::BUTTON_LB, [this](...) {
        this->pageGuide(-1);
        return true;
    });

    this->registerAction("Guide +30m", brls::BUTTON_RB, [this](...) {
        this->pageGuide(1);
        return true;
    });

    statusLabel->setText(fmt::format("{} channels - logos load as available", this->channelsList.size()));
    return;
}

brls::View* HomeLive::getDefaultFocus() {
    if (this->setupPanel && this->setupPanel->getVisibility() == brls::Visibility::VISIBLE && this->setupM3uButton) {
        return this->setupM3uButton;
    }
    if (this->upRecyclingGrid && this->upRecyclingGrid->getVisibility() == brls::Visibility::VISIBLE) {
        if (auto* focus = this->upRecyclingGrid->getDefaultFocus()) return focus;
    }
    return this->recyclingGrid ? this->recyclingGrid->getDefaultFocus() : nullptr;
}

void HomeLive::selectGroupIndex(size_t index) {
    auto* datasource = dynamic_cast<DataSourceUpList*>(upRecyclingGrid->getDataSource());
    if (!datasource) return;
    if (index >= datasource->getItemCount()) return;
    this->selectedGroupIndex = index;
    datasource->setSelectedIndex(upRecyclingGrid, index);
    upRecyclingGrid->selectRowAt(index, false);

    std::string selectedGroup = datasource->getGroupNameByIndex(index);
    tsvitch::LiveM3u8ListResult filtered;
    {
        std::lock_guard<std::mutex> lock(groupCacheMutex);
        if (groupCache.count(selectedGroup)) {
            filtered = groupCache[selectedGroup];
        } else {
            for (const auto& item : this->channelsList) {
                if (item.groupTitle == selectedGroup) filtered.push_back(item);
            }
            groupCache[selectedGroup] = filtered;
        }
    }
    this->showChannels(filtered);

    brls::Logger::debug("selectGroupIndex: {}", index);
}

void HomeLive::showChannels(tsvitch::LiveM3u8ListResult channels) {
    if (channels.empty()) {
        visibleChannels.clear();
        recyclingGrid->setEmpty();
        return;
    }
    visibleChannels = channels;
    recyclingGrid->setDataSource(new DataSourceLiveVideoList(channels, guideStart));
}

void HomeLive::pageGuide(int direction) {
    guideStart += direction * 30 * 60;
    brls::Logger::info("HomeLive: guide window moved to {}", formatGuideTime(guideStart));
    this->updateGuideHeader();

    auto channels = this->visibleChannels.empty() ? this->channelsList : this->visibleChannels;
    this->showChannels(std::move(channels));
}

void HomeLive::updateGuideHeader() {
    if (this->guideHeader0) this->guideHeader0->setText(formatGuideHeaderTime(guideStart));
    if (this->guideHeader1) this->guideHeader1->setText(formatGuideHeaderTime(guideStart + GUIDE_SLOT_SECONDS));
    if (this->guideHeader2) this->guideHeader2->setText(formatGuideHeaderTime(guideStart + (2 * GUIDE_SLOT_SECONDS)));
}

void HomeLive::toggleFavorite() {
    auto* item = dynamic_cast<LiveGuideRowCell*>(this->recyclingGrid->getFocusedItem());
    if (!item) return;
    tsvitch::LiveM3u8 channel = item->getChannel();
    if (channel.url.empty()) return;
    bool willFavorite = !FavoriteManager::get()->isFavorite(channel.url);
    FavoriteManager::get()->toggle(channel);
    brls::Application::notify(willFavorite ? "Added to Favorites" : "Removed from Favorites");
    {
        std::lock_guard<std::mutex> lock(groupCacheMutex);
        groupCache["Favorites"] = FavoriteManager::get()->getFavorites();
        sortChannelsAlphabetically(groupCache["Favorites"]);
    }
    this->showChannels(this->visibleChannels);
}

void HomeLive::openLiveSettings() {
    Intent::openSettings([this]() {
        tsvitch::EpgManager::instance().loadFromUrl(ProgramConfig::instance().getEpgUrl(), [this]() {
            if (this->recyclingGrid) this->recyclingGrid->reloadData();
        });
        if (ProgramConfig::instance().getM3U8Url().empty()) {
            this->showInitialSetup();
        }
    });
}

void HomeLive::openPremiumInfo() {
    auto* dialog = new brls::Dialog(
        "PocketTV Premium uses an activation code from the subscription website.\n\n"
        "Choose PocketTV Premium in Live TV settings and enter your code. PocketTV will verify the code each time Premium mode loads.");
    dialog->addButton("Open Settings", [this]() {
        ProgramConfig::instance().setSettingItem(SettingItem::IPTV_MODE, 2);
        this->openLiveSettings();
    });
    dialog->addButton("OK", []() {});
    dialog->open();
}

void HomeLive::verifyPremiumSubscriptionAndLoad() {
    std::string code = normalizeActivationCode(
        ProgramConfig::instance().getSettingItem(SettingItem::PREMIUM_ACTIVATION_CODE, std::string{""}));

    if (code.size() != 8) {
        statusLabel->setText("Enter your Premium activation code");
        recyclingGrid->setEmpty("Premium activation required");
        upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        hideInitialSetup();
        this->openPremiumInfo();
        return;
    }

    hideInitialSetup();
    statusLabel->setText("Verifying PocketTV Premium...");
    recyclingGrid->setEmpty("Checking subscription");
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);

    nlohmann::json body = {{"code", code}};
    cpr::PostCallback(
        [this, validityFlag = this->validityFlag](const cpr::Response& r) {
            if (!validityFlag || !validityFlag->load()) return;

            std::string errorMessage;
            std::string m3uUrl;
            std::string epgUrl;

            if (r.error) {
                errorMessage = "Premium verification failed. Check your network connection.";
                brls::Logger::error("Premium redeem network error: {}", r.error.message);
            } else if (r.status_code < 200 || r.status_code >= 300) {
                errorMessage = "Premium subscription inactive or activation code invalid.";
                brls::Logger::warning("Premium redeem HTTP error: {}", r.status_code);
            } else {
                auto json = nlohmann::json::parse(r.text, nullptr, false);
                if (json.is_discarded() || !json.contains("m3uUrl") || !json.contains("epgUrl") ||
                    !json["m3uUrl"].is_string() || !json["epgUrl"].is_string()) {
                    errorMessage = "Premium verification response was incomplete.";
                    brls::Logger::error("Premium redeem response missing required URLs");
                } else {
                    m3uUrl = json["m3uUrl"].get<std::string>();
                    epgUrl = json["epgUrl"].get<std::string>();
                    if (m3uUrl.empty() || epgUrl.empty()) {
                        errorMessage = "Premium verification response was incomplete.";
                    }
                }
            }

            brls::sync([this, validityFlag, errorMessage, m3uUrl, epgUrl]() {
                if (!validityFlag || !validityFlag->load()) return;

                if (!errorMessage.empty()) {
                    ProgramConfig::instance().setSettingItem(SettingItem::PREMIUM_M3U_URL, std::string{""});
                    ProgramConfig::instance().setSettingItem(SettingItem::PREMIUM_EPG_URL, std::string{""});
                    ProgramConfig::instance().setM3U8Url("");
                    ProgramConfig::instance().setEpgUrl("");
                    ChannelManager::get()->remove();
                    statusLabel->setText("PocketTV Premium unavailable");
                    recyclingGrid->setError(errorMessage);
                    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
                    return;
                }

                ProgramConfig::instance().setSettingItem(SettingItem::PREMIUM_M3U_URL, m3uUrl);
                ProgramConfig::instance().setSettingItem(SettingItem::PREMIUM_EPG_URL, epgUrl);
                ProgramConfig::instance().setM3U8Url(m3uUrl);
                ProgramConfig::instance().setEpgUrl(epgUrl);
                ChannelManager::get()->remove();

                statusLabel->setText("Premium active. Loading channels...");
                tsvitch::EpgManager::instance().loadFromUrl(epgUrl, [this]() {
                    if (this->recyclingGrid) this->recyclingGrid->reloadData();
                });
                this->requestLiveList();
            });
        },
        cpr::Url{PREMIUM_REDEEM_URL},
        cpr::Header{{"Content-Type", "application/json"}, {"Accept", "application/json"}, {"User-Agent", "PocketTV"}},
        cpr::Body{body.dump()},
        cpr::Timeout{15000});
}

void HomeLive::installForwarder() {
    auto* dialog = new brls::Dialog(
        "Install the PocketTV forwarder on the Switch home screen?\n\n"
        "This creates a full-application launcher that opens /switch/PocketTV.nro. Installing unsigned NSP content may carry account or console risk.");
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("Install", []() {
#ifdef BUILTIN_NSP
        brls::Application::blockInputs();
        mini::InstallSD("romfs:/nsp_forwarder.nsp");
        unsigned long long appTitleID = mini::GetTitleID();
        appletRequestLaunchApplication(appTitleID, NULL);
#else
        brls::Application::notify("This build does not include the forwarder installer");
#endif
    });
    dialog->open();
}

void HomeLive::showInitialSetup() {
    statusLabel->setText("PocketTV setup required");
    recyclingGrid->setEmpty("");
    recyclingGrid->setVisibility(brls::Visibility::GONE);
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    if (setupPanel) setupPanel->setVisibility(brls::Visibility::VISIBLE);
    if (setupM3uButton) brls::Application::giveFocus(setupM3uButton);
}

void HomeLive::hideInitialSetup() {
    if (setupPanel) setupPanel->setVisibility(brls::Visibility::GONE);
    if (recyclingGrid) recyclingGrid->setVisibility(brls::Visibility::VISIBLE);
}

void HomeLive::search() {
    brls::Application::getImeManager()->openForText([this](const std::string& text) { this->filter(text); },
                                                    "tsvitch/home/common/search"_i18n, "", 32, "", 0);
}

void HomeLive::cancelSearch() {
    isSearchActive = false;
    this->showChannels(this->channelsList);
    upRecyclingGrid->setVisibility(brls::Visibility::VISIBLE);
    this->selectGroupIndex(this->selectedGroupIndex);
}

void HomeLive::filter(const std::string& key) {
    if (key.empty()) return;

    isSearchActive = true;

    brls::Threading::sync([this, key]() {
        auto* datasource = dynamic_cast<DataSourceLiveVideoList*>(recyclingGrid->getDataSource());
        if (datasource) {
            tsvitch::LiveM3u8ListResult filtered;
            std::string lowerKey = key;
            std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            for (const auto& item : this->channelsList) {
                std::string lowerTitle = item.title;
                std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                std::string lowerGroupTitle = item.groupTitle;
                std::transform(lowerGroupTitle.begin(), lowerGroupTitle.end(), lowerGroupTitle.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (lowerTitle.find(lowerKey) != std::string::npos ||
                    lowerGroupTitle.find(lowerKey) != std::string::npos)
                    filtered.push_back(item);
            }
            sortChannelsAlphabetically(filtered);
            if (filtered.empty()) {
                recyclingGrid->setEmpty();
            } else {
                this->showChannels(filtered);
            }
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        }
    });
}

void HomeLive::onShow() {
    brls::Logger::info("Fragment HomeLive: onShow called");
    
    // Se il caricamento iniziale è ancora in corso, non fare nulla
    if (isInitialLoadInProgress) {
        brls::Logger::debug("HomeLive onShow: Initial load still in progress, skipping");
        return;
    }
    
    // Smart refresh: controlla se abbiamo già canali in memoria
    if (!channelsList.empty()) {
        brls::Logger::debug("HomeLive onShow: Already have {} channels in memory, checking if refresh needed", channelsList.size());
        
        // Per decidere se ricaricare, controlla l'età della cache
        int iptvMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0);
        if (iptvMode == 2) {
            brls::Logger::info("HomeLive onShow: Premium mode active, verifying subscription");
            this->verifyPremiumSubscriptionAndLoad();
            return;
        }
        int maxCacheAge = (iptvMode == 1) ? 5 : 15; // Xtream: 5 min, M3U8: 15 min
        
        brls::Threading::async([this, maxCacheAge, iptvMode, validityFlag = this->validityFlag] {
            // Controlla se l'app è ancora valida prima di procedere
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive onShow: async task canceled - app exiting");
                return;
            }
            
            bool needsRefresh = !ChannelManager::get()->isCacheValid(maxCacheAge);
            
            brls::sync([this, needsRefresh, iptvMode, validityFlag]() {
                // Controlla di nuovo la validità prima di aggiornare l'UI
                if (!validityFlag || !validityFlag->load()) {
                    brls::Logger::debug("HomeLive onShow: sync task canceled - app exiting");
                    return;
                }
                
                if (needsRefresh) {
                    brls::Logger::info("HomeLive onShow: Cache expired, refreshing channels (IPTV mode: {})", iptvMode);
                    this->requestLiveList();
                } else {
                    brls::Logger::debug("HomeLive onShow: Cache still valid, no refresh needed");
                    // Solo ricarica i dati delle grid per aggiornare la UI
                    this->recyclingGrid->reloadData();
                    this->upRecyclingGrid->reloadData();
                }
            });
        });
        return;
    }
    
    // Se non abbiamo canali e il caricamento iniziale non è in corso, usa lo stesso meccanismo del costruttore
    brls::Logger::debug("HomeLive onShow: No channels in memory and no initial load in progress, loading...");
    
    int iptvMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0);
    brls::Threading::async([this, iptvMode, validityFlag = this->validityFlag] {
        // Controlla se l'app è ancora valida prima di procedere
        if (!validityFlag || !validityFlag->load()) {
            brls::Logger::debug("HomeLive onShow: fallback async task canceled - app exiting");
            return;
        }
        
        auto cachedChannels = ChannelManager::get()->loadIfValid();
        
        brls::sync([this, cachedChannels, validityFlag]() {
            // Controlla di nuovo la validità prima di aggiornare l'UI
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive onShow: fallback sync task canceled - app exiting");
                return;
            }
            
            if (ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 2) {
                brls::Logger::info("HomeLive onShow: Premium mode active, verifying subscription");
                this->verifyPremiumSubscriptionAndLoad();
            } else if (!cachedChannels.empty()) {
                brls::Logger::info("HomeLive onShow: Using valid cached channels ({} channels)", cachedChannels.size());
                this->onLiveList(cachedChannels, false);
            } else if (ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 0 &&
                       ProgramConfig::instance().getM3U8Url().empty()) {
                brls::Logger::info("HomeLive onShow: no M3U8 URL configured");
                this->statusLabel->setText("PocketTV setup required");
                this->showInitialSetup();
            } else {
                brls::Logger::info("HomeLive onShow: No valid cache, requesting fresh channels");
                this->requestLiveList();
            }
        });
    });
    
    brls::Logger::debug("HomeLive onShow: onShow completed");
}

void HomeLive::onCreate() {
    brls::Logger::debug("Fragment HomeLive: onCreate called");

    // Non fare niente qui - il caricamento è già gestito nel costruttore
    // in modo completamente asincrono per evitare blocchi dell'UI
    brls::Logger::debug("HomeLive onCreate: Delegating to constructor for async loading");
}

    // for (int i = 0; i < 100; ++i) {
    //     // Crea la sidebar item (puoi personalizzare label e stile)
    //    auto* item = new AutoSidebarItem();
    //         item->setTabStyle(AutoTabBarStyle::PLAIN);
    //         item->setLabel("Tab " + std::to_string(i + 1));
    //         item->setFontSize(18);

    //     // Funzione che crea la view associata al tab
    //        this->tabFrame->addTab(item, [this, i, item]() {
    //         // Qui puoi restituire una view diversa per ogni tab
    //         // Esempio: una semplice Box con un'etichetta
    //         auto* box = new brls::Box();
    //         auto* label = new brls::Label();
    //         label->setText("Contenuto Tab " + std::to_string(i + 1));
    //         box->addView(label);
    //         return box;
    //     });
    // }

HomeLive::~HomeLive() { 
    brls::Logger::debug("Fragment HomeLiveActivity: delete");
    
    // Cancella la sottoscrizione all'evento di uscita solo se è stata creata
    if (hasExitSubscription) {
        brls::Application::getExitEvent()->unsubscribe(exitEventSubscription);
    }
    
    // Invalidate the flag to prevent callbacks from accessing this object
    if (validityFlag) {
        validityFlag->store(false);
    }
}

void HomeLive::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                    brls::FrameContext* ctx) {
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    if (this->channelsList.empty()) return;

    const auto& state = brls::Application::getControllerState();
    bool leftDown    = state.buttons[brls::BUTTON_LB];
    bool rightDown   = state.buttons[brls::BUTTON_RB];

    if (leftDown && !this->guideLeftPressed) {
        this->pageGuide(-1);
    }
    if (rightDown && !this->guideRightPressed) {
        this->pageGuide(1);
    }

    this->guideLeftPressed  = leftDown;
    this->guideRightPressed = rightDown;
}

brls::View* HomeLive::create() { 
    brls::Logger::debug("HomeLive::create() called - creating new HomeLive instance");
    return new HomeLive(); 
}

void HomeLive::downloadVideo() {
    // Ottieni l'item attualmente focalizzato
    auto* item = dynamic_cast<LiveGuideRowCell*>(this->recyclingGrid->getFocusedItem());
    if (!item) {
        brls::Logger::warning("HomeLive::downloadVideo: No focused item");
        return;
    }

    // Ottieni il canale
    tsvitch::LiveM3u8 channel = item->getChannel();
    
    // Controlla se è una live stream in corso
    if (tsvitch::isLiveStream(channel.url, channel.title)) {
        brls::Logger::warning("HomeLive: Cannot download live streams");
        tsvitch::showLiveStreamDownloadError();
        return;
    }
    
    // Avvia il download
    std::string downloadId = DownloadManager::instance().startDownload(
        channel.title, 
        channel.url, 
        channel.logo,  // URL dell'immagine
        [](const std::string& id, float progress, size_t downloaded, size_t total) {
            // Callback di progresso - aggiorna il manager globale
            std::string progressText = fmt::format("{:.1f}%", progress);
            std::string statusText = fmt::format("{} / {} bytes", downloaded, total);
            
            brls::sync([id, progress, progressText, statusText]() {
                tsvitch::DownloadProgressManager::getInstance()->updateProgress(
                    id, progress, statusText, progressText
                );
            });
            
            brls::Logger::debug("Download {}: {:.1f}% ({}/{} bytes)", id, progress, downloaded, total);
        },
        [](const std::string& id, const std::string& filePath) {
            // Callback di completamento
            brls::Logger::info("Download {} completed: {}", id, filePath);
            
            brls::sync([id, filePath]() {
                // Nascondi l'overlay
                tsvitch::DownloadProgressManager::getInstance()->hideDownloadProgress(id);
                
                // Non mostrare notifica se è un download già completato (duplicato)
                if (filePath != "Already completed") {
                    brls::Application::notify("Download completato!");
                } else {
                    brls::Application::notify("File già scaricato!");
                }
            });
        },
        [](const std::string& id, const std::string& error) {
            // Callback di errore
            brls::Logger::error("Download {} failed: {}", id, error);
            brls::sync([id, error]() {
                // Nascondi l'overlay
                tsvitch::DownloadProgressManager::getInstance()->hideDownloadProgress(id);
                brls::Application::notify("Errore download: " + error);
            });
        }
    );
    
    if (!downloadId.empty()) {
        // Controlla lo stato del download per vedere se è già completato
        auto downloadItem = DownloadManager::instance().getDownload(downloadId);
        
        if (downloadItem.status == DownloadStatus::COMPLETED) {
            // È un download già completato, non mostrare overlay
            brls::Logger::info("HomeLive: Skipped showing overlay for already completed download {} ({})", downloadId, channel.title);
        } else {
            // È un nuovo download o uno in corso, mostra l'overlay
            tsvitch::DownloadProgressManager::getInstance()->showDownloadProgress(
                downloadId, channel.title, channel.url
            );
            
            brls::Application::notify("Download avviato: " + channel.title);
            brls::Logger::info("HomeLive: Started download {} for {}", downloadId, channel.title);
        }
    } else {
        brls::Application::notify("Errore nell'avvio del download");
        brls::Logger::error("HomeLive: Failed to start download for {}", channel.title);
    }
}
