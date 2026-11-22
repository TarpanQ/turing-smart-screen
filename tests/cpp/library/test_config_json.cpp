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
        "COM_PORT": "AUTO",
        "THEME": "3.5inchTheme2"
    },
    "display": {
        "REVISION": "A"
    }
})");
    config::update_json_file(file.path(), {
        {"config", "COM_PORT", "/dev/ttyACM0"},
        {"config", "ETH", ""},
        {"display", "BRIGHTNESS", 41},
        {"display", "DISPLAY_REVERSE", true},
        {"extra", "KEY", "value"},
    });

    const config::Json updated = config::load_json(file.path());
    EXPECT_EQ(updated["config"]["COM_PORT"], "/dev/ttyACM0");
    EXPECT_EQ(updated["config"]["THEME"], "3.5inchTheme2");
    EXPECT_EQ(updated["config"]["ETH"], "");
    EXPECT_EQ(updated["display"]["REVISION"], "A");
    EXPECT_EQ(updated["display"]["BRIGHTNESS"], 41);
    EXPECT_TRUE(updated["display"]["DISPLAY_REVERSE"].get<bool>());
    EXPECT_EQ(updated["extra"]["KEY"], "value");

    const std::string text = file.read();
    EXPECT_LT(text.find("COM_PORT"), text.find("THEME"));
    EXPECT_LT(text.find("THEME"), text.find("\"display\""));
    EXPECT_LT(text.find("REVISION"), text.find("BRIGHTNESS"));
    EXPECT_LT(text.find("BRIGHTNESS"), text.find("\"extra\""));
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
