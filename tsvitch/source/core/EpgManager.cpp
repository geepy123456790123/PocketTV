#include "core/EpgManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <tinyxml2.h>

#include "tsvitch/util/http.hpp"

namespace {

std::string textOf(tinyxml2::XMLElement* element) {
    if (!element || !element->GetText()) return "";
    return element->GetText();
}

std::string attrOf(tinyxml2::XMLElement* element, const char* name) {
    if (!element || !element->Attribute(name)) return "";
    return element->Attribute(name);
}

int daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

std::time_t utcTimestamp(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    return static_cast<std::time_t>(daysFromCivil(year, month, day)) * 86400 + hour * 3600 + minute * 60 + second;
}

}  // namespace

namespace tsvitch {

void EpgManager::loadFromUrl(const std::string& url) {
    if (url.empty()) return;
    brls::Threading::async([this, url]() {
        brls::Logger::info("EpgManager: downloading XMLTV data from {}", url);
        auto response = HTTP::get(url, {}, 60000);
        if (response.error || response.status_code < 200 || response.status_code >= 300) {
            brls::Logger::error("EpgManager: XMLTV download failed: {}", response.error.message);
            return;
        }
        this->parseXml(response.text);
    });
}

void EpgManager::parseXml(const std::string& xml) {
    tinyxml2::XMLDocument doc;
    auto result = doc.Parse(xml.c_str(), xml.size());
    if (result != tinyxml2::XML_SUCCESS) {
        brls::Logger::error("EpgManager: XMLTV parse failed with code {}", static_cast<int>(result));
        return;
    }

    std::map<std::string, std::vector<EpgProgramme>> parsed;
    auto* tv = doc.FirstChildElement("tv");
    if (!tv) return;

    for (auto* node = tv->FirstChildElement("programme"); node; node = node->NextSiblingElement("programme")) {
        EpgProgramme programme;
        programme.channelId = attrOf(node, "channel");
        programme.start     = parseXmltvTime(attrOf(node, "start"));
        programme.stop      = parseXmltvTime(attrOf(node, "stop"));
        programme.title     = textOf(node->FirstChildElement("title"));
        programme.subTitle  = textOf(node->FirstChildElement("sub-title"));
        programme.desc      = textOf(node->FirstChildElement("desc"));

        if (programme.channelId.empty() || programme.start == 0 || programme.stop == 0) continue;
        parsed[programme.channelId].push_back(programme);
    }

    for (auto& item : parsed) {
        std::sort(item.second.begin(), item.second.end(), [](const EpgProgramme& a, const EpgProgramme& b) {
            return a.start < b.start;
        });
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        programmes = std::move(parsed);
        loaded     = true;
    }
    brls::Logger::info("EpgManager: loaded XMLTV guide for {} channels", programmes.size());
}

bool EpgManager::isLoaded() {
    std::lock_guard<std::mutex> lock(mutex);
    return loaded;
}

EpgProgramme EpgManager::now(const std::string& channelId) {
    auto nowTime = std::time(nullptr);
    auto matches = window(channelId, nowTime, nowTime + 1);
    return matches.empty() ? EpgProgramme{} : matches.front();
}

std::vector<EpgProgramme> EpgManager::window(const std::string& channelId, std::time_t start, std::time_t stop) {
    std::vector<EpgProgramme> result;
    std::lock_guard<std::mutex> lock(mutex);
    auto found = programmes.find(channelId);
    if (found == programmes.end()) return result;

    for (const auto& programme : found->second) {
        if (programme.stop <= start) continue;
        if (programme.start >= stop) break;
        result.push_back(programme);
    }
    return result;
}

std::time_t EpgManager::parseXmltvTime(const std::string& value) {
    if (value.size() < 14) return 0;
    int year        = std::atoi(value.substr(0, 4).c_str());
    unsigned month  = std::atoi(value.substr(4, 2).c_str());
    unsigned day    = std::atoi(value.substr(6, 2).c_str());
    unsigned hour   = std::atoi(value.substr(8, 2).c_str());
    unsigned minute = std::atoi(value.substr(10, 2).c_str());
    unsigned second = std::atoi(value.substr(12, 2).c_str());

    std::time_t utc = utcTimestamp(year, month, day, hour, minute, second);
    auto pos = value.find_first_of("+-", 14);
    if (pos != std::string::npos && pos + 4 < value.size()) {
        int hours = std::atoi(value.substr(pos + 1, 2).c_str());
        int mins  = std::atoi(value.substr(pos + 3, 2).c_str());
        int offset = (hours * 3600) + (mins * 60);
        utc += value[pos] == '+' ? -offset : offset;
    }
    return utc;
}

}  // namespace tsvitch
