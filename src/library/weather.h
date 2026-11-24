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
 * @file weather.h
 * @brief Current weather from OpenWeatherMap: common interface of the providers and selection of the API version.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>

namespace weather {

/** @brief Current conditions displayed by the themes. A value that is not available is std::nullopt. */
struct Conditions
{
    std::optional<std::string> temp;      ///< Temperature with its unit, e.g. "17.5°C"
    std::optional<std::string> feel;      ///< Feels-like temperature in parentheses, e.g. "(17.2°C)"
    std::optional<std::string> desc;      ///< Weather description, or the error message when the API call fails
    std::optional<std::string> time;      ///< Local time of the update, e.g. "@15:33"
    std::optional<std::string> humidity;  ///< Relative humidity, e.g. "45%"
};

/** @brief OpenWeatherMap settings, read from the WEATHER_* keys of config.json. */
struct Settings
{
    std::string lat;      ///< WEATHER_LATITUDE
    std::string lon;      ///< WEATHER_LONGITUDE
    std::string api_key;  ///< WEATHER_API_KEY, key of the One Call API 3.0, empty when not set
    std::string app_id;   ///< WEATHER_APP_ID, key of the Current weather API 2.5, empty when not set
    std::string units;    ///< WEATHER_UNITS: metric, imperial or standard
    std::string lang;     ///< WEATHER_LANGUAGE
};

/** @brief Reads the WEATHER_* keys of config.json, with the default values of the theme. */
Settings settings_from_config();

/**
 * @brief Python str.capitalize(): first character upper case, the rest lower case.
 * @param text UTF-8 text
 * @return The capitalized UTF-8 text
 */
std::string capitalize(const std::string &text);

/**
 * @brief Weather provider: the contract used by the rest of the code.
 *
 * The implementations are Weather30 (One Call API 3.0) and Weather25 (Current weather data API 2.5).
 */
class Weather
{
public:
    /** @param settings OpenWeatherMap settings used for the requests */
    explicit Weather(Settings settings);
    virtual ~Weather() = default;
    Weather(const Weather &) = delete;
    Weather &operator=(const Weather &) = delete;

    /**
     * @brief Selects the provider: Weather30 when an API key is set, Weather25 otherwise.
     * @param settings OpenWeatherMap settings
     * @return The provider to use for every fetch
     */
    static std::unique_ptr<Weather> create(const Settings &settings);

    /**
     * @brief Fetches the current conditions. Does not throw: on failure, the description holds the error message.
     * @return The current conditions, with only the description set when no key is configured or the call fails
     */
    Conditions fetch() const;

protected:
    /**
     * @brief Request and parsing specific to the API version, only called when an API key is set.
     * @return The current conditions
     * @throws std::exception on a network or parsing error
     */
    virtual Conditions query() const = 0;

    /**
     * @brief HTTP GET request.
     * @param url Full URL of the request
     * @param status_code Set to the HTTP status code of the response
     * @return The body of the response
     * @throws std::runtime_error when the transfer fails
     */
    static std::string http_get(const std::string &url, long &status_code);

    /** @return The time of the update, in the local time zone, as "@HH:MM" */
    static std::string time_now();

    /** @return The degree symbol of the configured units: °C, °F or °K */
    std::string degree_symbol() const;

    Settings settings_;  ///< OpenWeatherMap settings used for the requests
};

}  // namespace weather
