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
 * @file weather25.cpp
 * @brief Current weather from the OpenWeatherMap "Current weather data" API 2.5 (free plan).
 */

#include "library/weather25.h"

#include <format>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "library/config.h"
#include "library/log.h"

namespace weather {

Weather25::Weather25(Settings settings) : Weather(std::move(settings)) {}

Conditions Weather25::parse(long status_code, const std::string &body) const
{
    // JSON is parsed with the YAML parser (JSON is a subset of YAML)
    const YAML::Node data = YAML::Load(body);
    Conditions conditions;
    if (status_code == 200) {
        const YAML::Node main = data["main"];
        const std::string deg = degree_symbol();
        conditions.temp = std::format("{:.1f}{}", main["temp"].as<double>(), deg);
        conditions.feel = std::format("({:.1f}{})", main["feels_like"].as<double>(), deg);
        conditions.humidity = std::format("{:.0f}%", main["humidity"].as<double>());
        conditions.desc = capitalize(data["weather"][0]["description"].as<std::string>());
        conditions.time = time_now();
    } else {
        logger.error("Error {} fetching OpenWeatherMap API:", status_code);
        conditions.desc = config::get(data, "message", "");
    }
    return conditions;
}

Conditions Weather25::query() const
{
    const std::string url = std::format("https://api.openweathermap.org/data/2.5/weather?lat={}&lon={}"
                                        "&appid={}&units={}&lang={}",
                                        settings_.lat, settings_.lon, settings_.app_id, settings_.units,
                                        settings_.lang);
    long status_code = 0;
    const std::string body = http_get(url, status_code);
    return parse(status_code, body);
}

}  // namespace weather
