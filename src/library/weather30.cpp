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
 * @file weather30.cpp
 * @brief Current weather from the OpenWeatherMap "One Call API" 3.0 (requires a subscription).
 */

#include "library/weather30.h"

#include <format>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "library/config.h"
#include "library/log.h"

namespace weather {

Weather30::Weather30(Settings settings) : Weather(std::move(settings)) {}

Conditions Weather30::parse(long status_code, const std::string &body) const
{
    // JSON is parsed with the YAML parser (JSON is a subset of YAML)
    const YAML::Node data = YAML::Load(body);
    Conditions conditions;
    if (status_code == 200) {
        const YAML::Node current = data["current"];
        const std::string deg = degree_symbol();
        conditions.temp = std::format("{:.1f}{}", current["temp"].as<double>(), deg);
        conditions.feel = std::format("({:.1f}{})", current["feels_like"].as<double>(), deg);
        conditions.desc = capitalize(current["weather"][0]["description"].as<std::string>());
        conditions.humidity = std::format("{:.0f}%", current["humidity"].as<double>());
        conditions.time = time_now();
    } else {
        logger.error("Error {} fetching OpenWeatherMap API:", status_code);
        conditions.desc = config::get(data, "message", "");
    }
    return conditions;
}

Conditions Weather30::query() const
{
    const std::string url = std::format("https://api.openweathermap.org/data/3.0/onecall?lat={}&lon={}"
                                        "&exclude=minutely,hourly,daily,alerts&appid={}&units={}&lang={}",
                                        settings_.lat, settings_.lon, settings_.api_key, settings_.units,
                                        settings_.lang);
    long status_code = 0;
    const std::string body = http_get(url, status_code);
    return parse(status_code, body);
}

}  // namespace weather
