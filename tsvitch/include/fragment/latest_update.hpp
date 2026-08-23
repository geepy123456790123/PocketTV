

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/box.hpp>

#include "view/user_info.hpp"

class ReleaseAuthor {
public:
    std::string login;
    std::string avatar_url;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ReleaseAuthor, login, avatar_url);

class ReleaseReaction {
public:
    int plus_one;
    int laugh;
    int hooray;
    int heart;
    int rocket;
    int eyes;
};
inline void from_json(const nlohmann::json& nlohmann_json_j, ReleaseReaction& nlohmann_json_t) {
    if (nlohmann_json_j.contains("+1")) {
        nlohmann_json_j.at("+1").get_to(nlohmann_json_t.plus_one);
    }
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, laugh, hooray, heart, rocket, eyes));
}

class ReleaseAsset {
public:
    std::string name;
    std::string browser_download_url;
    int64_t size = 0;
};
inline void from_json(const nlohmann::json& nlohmann_json_j, ReleaseAsset& nlohmann_json_t) {
    if (nlohmann_json_j.contains("name") && nlohmann_json_j.at("name").is_string())
        nlohmann_json_j.at("name").get_to(nlohmann_json_t.name);
    if (nlohmann_json_j.contains("browser_download_url") && nlohmann_json_j.at("browser_download_url").is_string())
        nlohmann_json_j.at("browser_download_url").get_to(nlohmann_json_t.browser_download_url);
    if (nlohmann_json_j.contains("size") && nlohmann_json_j.at("size").is_number_integer())
        nlohmann_json_j.at("size").get_to(nlohmann_json_t.size);
}

class ReleaseNote {
public:
    std::string tag_name;
    std::string name;
    std::string body;
    std::string published_at;
    ReleaseAuthor author{};
    ReleaseReaction reactions{};
    std::vector<ReleaseAsset> assets{};
};
inline void from_json(const nlohmann::json& nlohmann_json_j, ReleaseNote& nlohmann_json_t) {
    if (nlohmann_json_j.contains("reactions")) {
        nlohmann_json_j.at("reactions").get_to(nlohmann_json_t.reactions);
    }
    if (nlohmann_json_j.contains("assets") && nlohmann_json_j.at("assets").is_array()) {
        nlohmann_json_j.at("assets").get_to(nlohmann_json_t.assets);
    }
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, tag_name, name, body, published_at, author));
}

class LatestUpdate : public brls::Box {
public:
    explicit LatestUpdate(const ReleaseNote& info);

    ~LatestUpdate() override;

private:
    BRLS_BIND(brls::Label, subtitle, "latest/subtitle");
    BRLS_BIND(brls::Box, textbox, "latest/textbox");
    BRLS_BIND(brls::Box, topBox, "latest/top");
    BRLS_BIND(brls::Label, header, "latest/header");
};
