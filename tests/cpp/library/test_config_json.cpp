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
 * @file test_config_json.cpp
 * @brief Unit tests of the config.json functions of src/library/config (loading, values, updates).
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "library/config.h"

namespace {

// Temporary file with the given content, removed when the object is destroyed
class TempFile
{
public:
    explicit TempFile(const std::string &content)
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("test_config_json_" + std::to_string(counter++) + ".json");
        std::ofstream(path_) << content;
    }
    ~TempFile() { std::filesystem::remove(path_); }
    const std::filesystem::path &path() const { return path_; }

    std::string read() const
    {
        std::ifstream in(path_);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

private:
    std::filesystem::path path_;
};

}  // namespace

/** @brief Values are read with their type, and the default value is used for missing or null keys. */
TEST(ConfigJson, Get)
{
    const config::Json node = config::Json::parse(R"({"S": "text", "N": 45.75, "I": 41, "B": true, "NULL": null})");
    EXPECT_EQ(config::get(node, "S", "default"), "text");
    EXPECT_EQ(config::get(node, "N", ""), "45.75");
    EXPECT_EQ(config::get(node, "I", 0), 41);
    EXPECT_TRUE(config::get(node, "B", false));
    EXPECT_EQ(config::get(node, "MISSING", "default"), "default");
    EXPECT_EQ(config::get(node, "NULL", "default"), "default");
    EXPECT_FALSE(config::get(node, "MISSING", false));
}

/** @brief A value of the wrong type is an error. */
TEST(ConfigJson, GetInvalid)
{
    const config::Json node = config::Json::parse(R"({"B": "text"})");
    EXPECT_THROW(config::get(node, "B", false), std::runtime_error);
}

/** @brief A missing section gives an empty object, so that values fall back to their defaults. */
TEST(ConfigJson, Section)
{
    const config::Json root = config::Json::parse(R"({"config": {"PING": "1.1.1.1"}, "display": 3})");
    EXPECT_EQ(config::get(config::section(root, "config"), "PING", "x"), "1.1.1.1");
    EXPECT_EQ(config::get(config::section(root, "display"), "REVISION", "A"), "A");
    EXPECT_EQ(config::get(config::section(root, "missing"), "PING", "x"), "x");
}

/** @brief The file is updated in place: keys keep their order, missing keys and sections are added at the end. */
TEST(ConfigJson, UpdateKeepsOrder)
{
    TempFile file(R"({
    "config": {
        "HW_SENSORS": "AUTO",
        "ETH": "lo"
    },
    "displays": [
        {"THEME": "3.5inchTheme2"}
    ]
})");
    config::update_json_file(file.path(), {
        {"config", "HW_SENSORS", "STUB"},
        {"config", "WLO", ""},
        {"extra", "KEY", "value"},
    });

    const config::Json updated = config::load_json(file.path());
    EXPECT_EQ(updated["config"]["HW_SENSORS"], "STUB");
    EXPECT_EQ(updated["config"]["ETH"], "lo");
    EXPECT_EQ(updated["config"]["WLO"], "");
    EXPECT_EQ(updated["extra"]["KEY"], "value");
    EXPECT_EQ(updated["displays"][0]["THEME"], "3.5inchTheme2");

    const std::string text = file.read();
    EXPECT_LT(text.find("HW_SENSORS"), text.find("ETH"));
    EXPECT_LT(text.find("ETH"), text.find("WLO"));
    EXPECT_LT(text.find("\"config\""), text.find("\"displays\""));
    EXPECT_LT(text.find("\"displays\""), text.find("\"extra\""));
}

/** @brief A display is read from its element of the displays array, with the defaults of the optional keys. */
TEST(ConfigJson, DisplayFromJson)
{
    const config::Json node = config::Json::parse(
        R"({"THEME": "3.5inchTheme2", "REVISION": "A", "BRIGHTNESS": 41, "DISPLAY_REVERSE": true})");
    const config::DisplayConfig display = config::DisplayConfig::from_json(node);
    EXPECT_EQ(display.com_port, "AUTO");
    EXPECT_EQ(display.theme, "3.5inchTheme2");
    EXPECT_EQ(display.revision, "A");
    EXPECT_EQ(display.brightness, 41);
    EXPECT_TRUE(display.display_reverse);
    EXPECT_TRUE(display.reset_on_startup);
}

/** @brief The required keys of a display (theme, revision, brightness) must be present. */
TEST(ConfigJson, DisplayRequiredKeys)
{
    EXPECT_THROW(config::DisplayConfig::from_json(config::Json::parse(R"({"REVISION": "A", "BRIGHTNESS": 41})")),
                 std::exception);
    EXPECT_THROW(config::DisplayConfig::from_json(config::Json::parse(R"({"THEME": "t", "BRIGHTNESS": 41})")),
                 std::exception);
    EXPECT_THROW(config::DisplayConfig::from_json(config::Json::parse(R"({"THEME": "t", "REVISION": "A"})")),
                 std::exception);
}

/** @brief A display written to JSON is read back identically. */
TEST(ConfigJson, DisplayRoundTrip)
{
    config::DisplayConfig display;
    display.com_port = "/dev/ttyACM1";
    display.theme = "Cyberdeck";
    display.revision = "B";
    display.brightness = 80;
    display.display_reverse = true;
    display.reset_on_startup = false;

    const config::DisplayConfig read = config::DisplayConfig::from_json(display.to_json());
    EXPECT_EQ(read.com_port, "/dev/ttyACM1");
    EXPECT_EQ(read.theme, "Cyberdeck");
    EXPECT_EQ(read.revision, "B");
    EXPECT_EQ(read.brightness, 80);
    EXPECT_TRUE(read.display_reverse);
    EXPECT_FALSE(read.reset_on_startup);
}

/** @brief The displays array is required and must not be empty. */
TEST(ConfigJson, LoadDisplaysRejectsMissingOrEmptyArray)
{
    EXPECT_THROW(config::load_displays(config::Json::parse(R"({"config": {}})")), std::runtime_error);
    EXPECT_THROW(config::load_displays(config::Json::parse(R"({"displays": []})")), std::runtime_error);
    EXPECT_THROW(config::load_displays(config::Json::parse(R"({"displays": {"THEME": "t"}})")), std::runtime_error);
}

/** @brief Saving the displays replaces the displays array and keeps the other sections. */
TEST(ConfigJson, SaveDisplays)
{
    TempFile file(R"({"config": {"PING": "1.1.1.1"}, "displays": [{"THEME": "old", "REVISION": "A", "BRIGHTNESS": 1}]})");
    config::DisplayConfig first = config::DisplayConfig::from_json(
        config::Json::parse(R"({"THEME": "new", "REVISION": "C", "BRIGHTNESS": 2})"));
    config::DisplayConfig second = first;
    second.theme = "second";
    config::save_displays(file.path(), {first, second});

    const config::Json saved = config::load_json(file.path());
    EXPECT_EQ(saved["config"]["PING"], "1.1.1.1");
    ASSERT_EQ(saved["displays"].size(), 2u);
    EXPECT_EQ(saved["displays"][0]["THEME"], "new");
    EXPECT_EQ(saved["displays"][0]["REVISION"], "C");
    EXPECT_EQ(saved["displays"][1]["THEME"], "second");
}

/** @brief A file that is not a JSON object is rejected. */
TEST(ConfigJson, LoadRejectsNonObject)
{
    TempFile file("[1, 2, 3]");
    EXPECT_THROW(config::load_json(file.path()), std::runtime_error);
}

/** @brief Invalid JSON is rejected. */
TEST(ConfigJson, LoadRejectsInvalidJson)
{
    TempFile file("{\"config\": ");
    EXPECT_THROW(config::load_json(file.path()), std::exception);
}
