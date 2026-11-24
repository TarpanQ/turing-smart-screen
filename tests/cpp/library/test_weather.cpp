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
 * @file test_weather.cpp
 * @brief Unit tests of src/library/weather: parsing of the API responses and selection of the provider
 */

#include <gtest/gtest.h>

#include "library/weather.h"
#include "library/weather25.h"
#include "library/weather30.h"

namespace {

const char *ONECALL_30_BODY = R"({
  "lat": 50.69, "lon": 5.59, "timezone": "Europe/Brussels",
  "current": {
    "dt": 1795541367, "temp": 17.5, "feels_like": 17.2, "humidity": 45,
    "weather": [{"id": 800, "main": "Clear", "description": "clear sky", "icon": "01d"}]
  }
})";

const char *CURRENT_25_BODY = R"({
  "coord": {"lon": 5.59, "lat": 50.69},
  "weather": [{"id": 500, "main": "Rain", "description": "light rain", "icon": "10d"}],
  "main": {"temp": 17.5, "feels_like": 17.2, "temp_min": 16.0, "temp_max": 18.0, "pressure": 1013, "humidity": 45},
  "dt": 1795541367, "name": "Voroux-lez-Liers"
})";

const char *ERROR_BODY = R"({"cod": 401, "message": "Invalid API key"})";

weather::Settings settings(const std::string &api_key, const std::string &app_id = "",
                           const std::string &units = "metric")
{
    return weather::Settings{.lat = "50.69",
                             .lon = "5.59",
                             .api_key = api_key,
                             .app_id = app_id,
                             .units = units,
                             .lang = "en"};
}

}  // namespace

TEST(WeatherOneCall30, ParsesCurrentConditions)
{
    const weather::Weather30 provider(settings("key"));
    const weather::Conditions c = provider.parse(200, ONECALL_30_BODY);
    EXPECT_EQ(c.temp, "17.5°C");
    EXPECT_EQ(c.feel, "(17.2°C)");
    EXPECT_EQ(c.humidity, "45%");
    EXPECT_EQ(c.desc, "Clear sky");
    ASSERT_TRUE(c.time.has_value());
    EXPECT_EQ(c.time->front(), '@');
}

TEST(WeatherOneCall30, ReturnsApiMessageOnError)
{
    const weather::Weather30 provider(settings("key"));
    const weather::Conditions c = provider.parse(401, ERROR_BODY);
    EXPECT_EQ(c.desc, "Invalid API key");
    EXPECT_FALSE(c.temp.has_value());
    EXPECT_FALSE(c.feel.has_value());
    EXPECT_FALSE(c.humidity.has_value());
    EXPECT_FALSE(c.time.has_value());
}

TEST(WeatherCurrent25, ParsesCurrentConditions)
{
    const weather::Weather25 provider(settings("", "key"));
    const weather::Conditions c = provider.parse(200, CURRENT_25_BODY);
    EXPECT_EQ(c.temp, "17.5°C");
    EXPECT_EQ(c.feel, "(17.2°C)");
    EXPECT_EQ(c.humidity, "45%");
    EXPECT_EQ(c.desc, "Light rain");
    ASSERT_TRUE(c.time.has_value());
    EXPECT_EQ(c.time->front(), '@');
}

TEST(WeatherCurrent25, ReturnsApiMessageOnError)
{
    const weather::Weather25 provider(settings("", "key"));
    const weather::Conditions c = provider.parse(401, ERROR_BODY);
    EXPECT_EQ(c.desc, "Invalid API key");
    EXPECT_FALSE(c.temp.has_value());
}

TEST(WeatherCurrent25, UsesUnitSymbol)
{
    const weather::Weather25 imperial(settings("", "key", "imperial"));
    EXPECT_EQ(imperial.parse(200, CURRENT_25_BODY).temp, "17.5°F");
    const weather::Weather25 standard(settings("", "key", "standard"));
    EXPECT_EQ(standard.parse(200, CURRENT_25_BODY).temp, "17.5°K");
}

TEST(WeatherProvider, SelectsOneCall30WhenKeyIsSet)
{
    const auto provider = weather::Weather::create(settings("key"));
    EXPECT_NE(dynamic_cast<weather::Weather30 *>(provider.get()), nullptr);
}

TEST(WeatherProvider, SelectsCurrent25WhenOnlyAppIdIsSet)
{
    const auto provider = weather::Weather::create(settings("", "app"));
    EXPECT_NE(dynamic_cast<weather::Weather25 *>(provider.get()), nullptr);
}

TEST(WeatherProvider, SelectsOneCall30WhenBothKeysAreSet)
{
    const auto provider = weather::Weather::create(settings("key", "app"));
    EXPECT_NE(dynamic_cast<weather::Weather30 *>(provider.get()), nullptr);
}

TEST(WeatherProvider, FetchWithoutAnyKeyReturnsMessageWithoutRequest)
{
    const auto provider = weather::Weather::create(settings("", ""));
    const weather::Conditions c = provider->fetch();
    EXPECT_EQ(c.desc, "No OpenWeatherMap API key Or app id");
    EXPECT_FALSE(c.temp.has_value());
}

TEST(WeatherHelpers, CapitalizeLikePythonStr)
{
    EXPECT_EQ(weather::capitalize("clear SKY"), "Clear sky");
    EXPECT_EQ(weather::capitalize(""), "");
}
