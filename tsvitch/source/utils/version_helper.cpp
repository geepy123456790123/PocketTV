#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <fmt/format.h>
#include <cpr/cpr.h>
#include <pystring.h>
#include <borealis/core/i18n.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/platforms/desktop/steam_deck.hpp>

#include "utils/config_helper.hpp"
#include "utils/dialog_helper.hpp"
#include "api/tsvitch/util/http.hpp"
#include "fragment/latest_update.hpp"

using namespace brls::literals;

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

namespace {
constexpr const char* POCKETTV_NRO_PATH = "/switch/PocketTV.nro";
constexpr const char* POCKETTV_UPDATE_PATH = "/switch/PocketTV.nro.update";
constexpr const char* POCKETTV_SDMC_NRO_PATH = "sdmc:/switch/PocketTV.nro";

std::string launchNroPath;

std::string findSwitchNroAsset(const ReleaseNote& info) {
    for (const auto& asset : info.assets) {
        if (asset.name == "PocketTV.nro" && !asset.browser_download_url.empty()) return asset.browser_download_url;
    }
    return "https://github.com/geepy123456790123/PocketTV/releases/latest/download/PocketTV.nro";
}

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

bool copyFile(const std::string& from, const std::string& to) {
    std::ifstream input(from, std::ios::binary);
    if (!input) {
        brls::Logger::error("Cannot open {} for reading: {}", from, std::strerror(errno));
        return false;
    }

    std::ofstream output(to, std::ios::binary | std::ios::trunc);
    if (!output) {
        brls::Logger::error("Cannot open {} for writing: {}", to, std::strerror(errno));
        return false;
    }

    output << input.rdbuf();
    output.close();
    input.close();

    if (!output) {
        brls::Logger::error("Failed while writing {}", to);
        return false;
    }

    return true;
}

bool sameFileContents(const std::string& first, const std::string& second) {
    std::ifstream a(first, std::ios::binary);
    std::ifstream b(second, std::ios::binary);
    if (!a || !b) return false;

    constexpr std::size_t bufferSize = 64 * 1024;
    std::vector<char> bufferA(bufferSize);
    std::vector<char> bufferB(bufferSize);

    while (a && b) {
        a.read(bufferA.data(), (std::streamsize)bufferA.size());
        b.read(bufferB.data(), (std::streamsize)bufferB.size());

        const auto readA = a.gcount();
        const auto readB = b.gcount();
        if (readA != readB) return false;
        if (readA == 0) break;
        if (!std::equal(bufferA.begin(), bufferA.begin() + readA, bufferB.begin())) return false;
    }

    return true;
}

bool pathParentExists(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return true;
    const std::string parent = path.substr(0, slash);
    if (parent.empty()) return true;

    struct stat info {};
    return stat(parent.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

std::vector<std::string> getNroPathCandidates() {
    std::vector<std::string> paths;
    auto addPath = [&paths](const std::string& path) {
        if (path.empty()) return;
        for (const auto& existing : paths) {
            if (existing == path) return;
        }
        paths.push_back(path);
    };

    addPath(launchNroPath);
    if (launchNroPath.rfind("sdmc:", 0) == 0) addPath(launchNroPath.substr(5));
    if (launchNroPath.rfind("/switch/", 0) == 0) addPath("sdmc:" + launchNroPath);
    addPath(POCKETTV_NRO_PATH);
    addPath(POCKETTV_SDMC_NRO_PATH);
    return paths;
}

std::string getPreferredNroPath() {
    auto paths = getNroPathCandidates();
    return paths.empty() ? POCKETTV_NRO_PATH : paths.front();
}

std::string getPreferredUpdatePath() { return getPreferredNroPath() + ".update"; }

std::string findPendingUpdatePath() {
    for (const auto& nroPath : getNroPathCandidates()) {
        auto updatePath = nroPath + ".update";
        if (fileExists(updatePath)) return updatePath;
    }
    if (fileExists(POCKETTV_UPDATE_PATH)) return POCKETTV_UPDATE_PATH;
    return "";
}
}  // namespace

APPVersion::APPVersion() {
    git_commit = std::string(STR(BUILD_TAG_SHORT));
    git_tag    = std::string(STR(BUILD_TAG_VERSION));
    major      = atoi(STR(BUILD_VERSION_MAJOR));
    minor      = atoi(STR(BUILD_VERSION_MINOR));
    revision   = atoi(STR(BUILD_VERSION_REVISION));
}

std::string APPVersion::getVersionStr() { return fmt::format("{}.{}.{}", major, minor, revision); }

std::string APPVersion::getPlatform() {
#ifdef IOS
    return "iOS";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(PS4)
    return "PS4";
#elif defined(__linux__)
    if (brls::isSteamDeck()) return "SteamDeck";
    return "Linux";
#elif defined(__WINRT__)
    return "UWP";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__SWITCH__)
#ifdef BOREALIS_USE_DEKO3D
    return "NX-deko3d";
#else
    return "NX";
#endif
#elif defined(__PSV__)
    return "PSVita";
#else
    return "Unknown";
#endif
}

std::string APPVersion::getPackageName() { return std::string{STR(BUILD_PACKAGE_NAME)}; }

bool APPVersion::needUpdate(std::string latestVersion) {
    if (latestVersion.length() < 5) brls::Application::quit();
    if (latestVersion[0] == 'v') latestVersion = latestVersion.substr(1, latestVersion.length() - 1);
    std::vector<std::string> v;
    pystring::split(latestVersion, v, ".");
    if (v.size() < 3) {
        brls::Logger::error("Cannot parse version info: {}", latestVersion);
        return false;
    }
    if (atoi(v[0].c_str()) > major) return true;
    if (atoi(v[1].c_str()) > minor) return true;
    if (atoi(v[2].c_str()) > revision) return true;
    return false;
}

void APPVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    static bool checking_update = false;
    if (checking_update) return;
    checking_update = true;
    brls::Threading::delay(delay, [showUpToDateDialog]() {
        std::string url =
            ProgramConfig::instance().getSettingItem(SettingItem::CUSTOM_UPDATE_API, APPVersion::RELEASE_API);

        cpr::GetCallback(
            [showUpToDateDialog](cpr::Response r) {
                checking_update = false;
                try {
                    if (showUpToDateDialog && r.status_code == 403) {
                        if (const nlohmann::json res = nlohmann::json::parse(r.text); res.contains("message")) {
                            auto msg = res.at("message").get<std::string>();
                            brls::sync([msg]() { brls::Application::notify(msg); });
                        }
                        return;
                    }
                    if (r.status_code != 200 || r.text.empty()) {
                        brls::Logger::error("Cannot check update: {} {}", r.status_code, r.error.message);
                        if (showUpToDateDialog) {
                            auto msg = r.reason;
                            brls::sync([msg]() { brls::Application::notify(msg); });
                        }
                        return;
                    }
                    const nlohmann::json res = nlohmann::json::parse(r.text);
                    auto info                = res.get<ReleaseNote>();
                    if (info.tag_name.empty()) {
                        brls::Logger::error("Cannot parse update info, tag_name is empty");
                        return;
                    }
                    if (!APPVersion::instance().needUpdate(info.tag_name)) {
                        brls::Logger::info("App is up to date");
                        if (showUpToDateDialog) {
                            brls::sync(
                                []() { brls::Application::notify("tsvitch/setting/tools/others/up2date"_i18n); });
                        }
                        return;
                    }
                    brls::sync([info]() {
                        auto container = new LatestUpdate(info);
                        auto dialog    = new brls::Dialog((brls::Box*)container);
                        const std::string downloadUrl = findSwitchNroAsset(info);
                        dialog->addButton("Later", []() {});
                        dialog->addButton("Download update", [downloadUrl]() {
                            APPVersion::instance().downloadUpdate(downloadUrl);
                        });
                        dialog->open();
                    });
                } catch (const std::exception& e) {
                    brls::Logger::error("check update failed: {} {} {}", r.status_code, r.text.c_str(), e.what());
                }
            },
            tsvitch::HTTP::VERIFY, tsvitch::HTTP::PROXIES, cpr::Url{url}, cpr::Timeout{10000});
    });
}

void APPVersion::downloadUpdate(const std::string& url) {
#ifndef __SWITCH__
    brls::Application::notify("Automatic NRO updates are only available on Nintendo Switch");
#else
    if (url.empty()) {
        brls::Application::notify("No PocketTV.nro asset found in the latest release");
        return;
    }

    brls::Application::notify("Downloading PocketTV update...");
    const std::string updatePath = getPreferredUpdatePath();
    cpr::GetCallback(
        [updatePath](cpr::Response r) {
            if (r.status_code != 200 || r.text.empty()) {
                brls::Logger::error("PocketTV update download failed: {} {}", r.status_code, r.error.message);
                brls::sync([]() { brls::Application::notify("Update download failed"); });
                return;
            }

            std::ofstream out(updatePath, std::ios::binary | std::ios::trunc);
            if (!out) {
                brls::Logger::error("Cannot write PocketTV update to {}", updatePath);
                brls::sync([updatePath]() { brls::Application::notify("Cannot write update to " + updatePath); });
                return;
            }
            out.write(r.text.data(), (std::streamsize)r.text.size());
            out.close();

            if (!out) {
                std::remove(updatePath.c_str());
                brls::sync([]() { brls::Application::notify("Update write failed"); });
                return;
            }

            brls::sync([]() {
                auto* dialog = new brls::Dialog(
                    "PocketTV update downloaded.\n\n"
                    "Automatic self-install is disabled for now to protect the launchable app.\n\n"
                    "To install it, close PocketTV and rename /switch/PocketTV.nro.update to /switch/PocketTV.nro from your computer or file manager.");
                dialog->addButton("hints/ok"_i18n, []() {});
                dialog->open();
            });
        },
        tsvitch::HTTP::VERIFY, tsvitch::HTTP::PROXIES, cpr::Url{url}, cpr::Timeout{120000}, cpr::Redirect{true},
        cpr::Header{{"User-Agent", "PocketTV-Updater"}});
#endif
}

void APPVersion::setExecutablePath(const char* path) {
#ifdef __SWITCH__
    launchNroPath = path ? path : "";
#else
    (void)path;
#endif
}

void APPVersion::applyPendingUpdate() {
#ifdef __SWITCH__
    const std::string updatePath = findPendingUpdatePath();
    if (updatePath.empty()) return;

    for (const auto& nroPath : getNroPathCandidates()) {
        if (!pathParentExists(nroPath)) continue;

        const std::string backupPath = nroPath + ".backup";
        std::remove(backupPath.c_str());

        if (fileExists(nroPath)) {
            if (!copyFile(nroPath, backupPath)) {
                brls::Logger::error("Failed to back up current PocketTV NRO from {} to {}", nroPath, backupPath);
                continue;
            }
        }

        if (!copyFile(updatePath, nroPath)) {
            brls::Logger::error("Failed to apply pending PocketTV update from {} to {}", updatePath, nroPath);
            if (fileExists(backupPath)) copyFile(backupPath, nroPath);
            continue;
        }

        if (!sameFileContents(updatePath, nroPath)) {
            brls::Logger::error("PocketTV update verification failed after copying {} to {}", updatePath, nroPath);
            if (fileExists(backupPath)) copyFile(backupPath, nroPath);
            continue;
        }

        std::remove(updatePath.c_str());
        brls::Logger::info("Applied pending PocketTV update from {} to {}", updatePath, nroPath);
        return;
    }

    brls::Logger::error("Pending PocketTV update exists at {}, but no launchable NRO path could be replaced",
                        updatePath);
#endif
}
