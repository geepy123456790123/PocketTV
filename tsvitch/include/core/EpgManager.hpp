#pragma once

#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <borealis/core/singleton.hpp>

namespace tsvitch {

class EpgProgramme {
public:
    std::string channelId;
    std::string title;
    std::string subTitle;
    std::string desc;
    std::time_t start = 0;
    std::time_t stop  = 0;
};

class EpgManager : public brls::Singleton<EpgManager> {
public:
    void loadFromUrl(const std::string& url);
    void loadFromUrl(const std::string& url, std::function<void()> onLoaded);
    void parseXml(const std::string& xml);
    bool isLoaded();
    std::string channelIcon(const std::string& channelId);
    EpgProgramme now(const std::string& channelId);
    std::vector<EpgProgramme> window(const std::string& channelId, std::time_t start, std::time_t stop);

private:
    static std::time_t parseXmltvTime(const std::string& value);
    std::map<std::string, std::vector<EpgProgramme>> programmes;
    std::map<std::string, std::string> channelIcons;
    std::mutex mutex;
    bool loaded = false;
};

}  // namespace tsvitch
