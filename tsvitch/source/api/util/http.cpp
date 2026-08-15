

#include "tsvitch/util/http.hpp"
#include <borealis.hpp> // or the specific header where brls::Logger is defined

namespace tsvitch {

cpr::Response HTTP::get(const std::string& url, const cpr::Parameters& parameters, int timeout) {
    return cpr::Get(
        cpr::Url{url},
        parameters,
        cpr::HttpVersion{cpr::HttpVersionCode::VERSION_2_0_TLS},
        cpr::Timeout{timeout},
        HTTP::HEADERS,
        HTTP::COOKIES,
        HTTP::PROXIES,
        HTTP::VERIFY);
}

void HTTP::setProxy(const std::string& proxyUrl) {
    if (proxyUrl.empty()) {
        HTTP::PROXIES = {};
        brls::Logger::info("Proxy disabled");
    } else {
        HTTP::PROXIES = cpr::Proxies{{"http", proxyUrl}, {"https", proxyUrl}};
        brls::Logger::info("Proxy configured: {}", proxyUrl);
    }
}

};  // namespace tsvitch
