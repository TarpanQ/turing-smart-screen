// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

/**
 * @file weather.cpp
 * @brief Current weather from OpenWeatherMap: common interface of the providers and selection of the API version.
 */

#include "library/weather.h"

#include <ctime>
#include <exception>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

#include <curl/curl.h>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

#include "library/config.h"
#include "library/log.h"
#include "library/platform.h"
#include "library/weather25.h"
#include "library/weather30.h"

namespace weather {

namespace {

size_t curl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    static_cast<std::string *>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

std::string capitalize(const std::string &text)
{
    icu::UnicodeString s = icu::UnicodeString::fromUTF8(text);
    s.toLower();
    if (s.length() > 0) {
        const UChar32 first = s.char32At(0);
        s.replace(0, U16_LENGTH(first), icu::UnicodeString(u_toupper(first)));
    }
    std::string result;
    s.toUTF8String(result);
    return result;
}

Settings settings_from_config()
{
    const config::Json &cfg = config::CONFIG_DATA;
    const config::Json section = config::section(cfg, "config");
    return Settings{
        .lat = config::get(section, "WEATHER_LATITUDE", ""),
        .lon = config::get(section, "WEATHER_LONGITUDE", ""),
        .api_key = config::get(section, "WEATHER_API_KEY", ""),
        .app_id = config::get(section, "WEATHER_APP_ID", ""),
        .units = config::get(section, "WEATHER_UNITS", "metric"),
        .lang = config::get(section, "WEATHER_LANGUAGE", "en"),
    };
}

Weather::Weather(Settings settings) : settings_(std::move(settings)) {}

std::unique_ptr<Weather> Weather::create(const Settings &settings)
{
    if (!settings.api_key.empty()) {
        return std::make_unique<Weather30>(settings);
    }
    return std::make_unique<Weather25>(settings);
}

Conditions Weather::fetch() const
{
    if (settings_.api_key.empty() && settings_.app_id.empty()) {
        logger.warning("No OpenWeatherMap API key or app id provided in config.json");
        Conditions conditions;
        conditions.desc = "No OpenWeatherMap API key Or app id";
        return conditions;
    }
    try {
        return query();
    } catch (const std::exception &e) {
        logger.error("Error fetching OpenWeatherMap API: {}", e.what());
        Conditions conditions;
        conditions.desc = "Error fetching OpenWeatherMap API";
        return conditions;
    }
}

std::string Weather::http_get(const std::string &url, long &status_code)
{
    std::string body;
    status_code = 0;
    CURL *curl = curl_easy_init();
    CURLcode res = CURLE_FAILED_INIT;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_easy_cleanup(curl);
    }
    if (res != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(res));
    }
    return body;
}

std::string Weather::time_now()
{
    const std::time_t now = std::time(nullptr);
    const std::tm tm = platform::local_time(now);
    return std::format("@{:02}:{:02}", tm.tm_hour, tm.tm_min);
}

std::string Weather::degree_symbol() const
{
    static const std::map<std::string, std::string> DEGREES = {
        {"metric", "°C"}, {"imperial", "°F"}, {"standard", "°K"}};
    const auto it = DEGREES.find(settings_.units);
    return it != DEGREES.end() ? it->second : "°?";
}

}  // namespace weather
