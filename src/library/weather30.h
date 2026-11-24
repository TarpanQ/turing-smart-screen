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
 * @file weather30.h
 * @brief Current weather from the OpenWeatherMap "One Call API" 3.0 (requires a subscription).
 */

#pragma once

#include <string>

#include "library/weather.h"

namespace weather {

/** @brief Provider using data/3.0/onecall: the current conditions, with the other data excluded. */
class Weather30 : public Weather
{
public:
    /** @param settings OpenWeatherMap settings used for the requests */
    explicit Weather30(Settings settings);

    /**
     * @brief Parses the response of data/3.0/onecall.
     * @param status_code HTTP status code of the response
     * @param body Body of the response (JSON)
     * @return The current conditions, or only the description holding the API error message when the status is not 200
     * @throws std::exception when the body cannot be parsed
     */
    Conditions parse(long status_code, const std::string &body) const;

protected:
    /** @copydoc Weather::query */
    Conditions query() const override;
};

}  // namespace weather
