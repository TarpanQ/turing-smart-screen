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
 * @file test_theme_items.cpp
 * @brief Unit tests of the display elements of a theme (src/library/theme_items)
 */

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "library/theme_items.h"

namespace {

// Default values of the stats used by the tests (same structure as res/themes/default.yaml)
const char *DEFAULT_THEME = R"(
STATS:
  CPU:
    INTERVAL: 0
    PERCENTAGE:
      INTERVAL: 0
      TEXT:
        SHOW: False
      GRAPH:
        SHOW: False
    FREQUENCY:
      INTERVAL: 0
      TEXT:
        SHOW: False
  GPU:
    INTERVAL: 0
    MEMORY:
      GRAPH:
        SHOW: False
    MEMORY_PERCENT:
      TEXT:
        SHOW: False
  NET:
    INTERVAL: 0
    WLO:
      UPLOAD:
        TEXT:
          SHOW: False
  CUSTOM:
    INTERVAL: 0
)";

// Theme with two measures: CPU/PERCENTAGE is shown, CPU/FREQUENCY is not. GPU has no form yet.
// Line numbers (0-based): PERCENTAGE TEXT = 6, FREQUENCY TEXT = 12, GPU = 16
const char *THEME_TEXT = R"(---
author: "test"
STATS:
  CPU:
    PERCENTAGE:
      INTERVAL: 1
      TEXT:
        SHOW: True  # shown on the display
        X: 10
        Y: 20
    FREQUENCY:
      INTERVAL: 5
      TEXT:
        SHOW: False
        X: 5
        Y: 6
  GPU:
    INTERVAL: 1
)";

YAML::Node parse(const std::string &text)
{
    return YAML::Load(text);
}

const std::vector<std::string> PERCENTAGE_TEXT = {"STATS", "CPU", "PERCENTAGE", "TEXT"};
const std::vector<std::string> PERCENTAGE_GRAPH = {"STATS", "CPU", "PERCENTAGE", "GRAPH"};
const std::vector<std::string> FREQUENCY_TEXT = {"STATS", "CPU", "FREQUENCY", "TEXT"};

std::vector<std::string> labels(const std::vector<theme_items::Item> &items)
{
    std::vector<std::string> result;
    for (const auto &item: items) {
        result.push_back(item.label);
    }
    return result;
}

}  // namespace

/** @brief Every form of the default stats is listed, deprecated ones and INTERVAL keys are not, custom sources are added. */
TEST(ThemeItems, AvailableItems)
{
    const std::vector<theme_items::CustomSource> custom = {{"ExampleCustomNumericData", true},
                                                           {"ExampleCustomTextOnlyData", false}};
    const auto items = theme_items::available_items(parse(DEFAULT_THEME), custom);
    const std::vector<std::string> expected = {
        "CPU / PERCENTAGE / TEXT",
        "CPU / PERCENTAGE / GRAPH",
        "CPU / FREQUENCY / TEXT",
        "GPU / MEMORY_PERCENT / TEXT",
        "NET / WLO / UPLOAD / TEXT",
        "CUSTOM / ExampleCustomNumericData / TEXT",
        "CUSTOM / ExampleCustomNumericData / GRAPH",
        "CUSTOM / ExampleCustomNumericData / RADIAL",
        "CUSTOM / ExampleCustomNumericData / LINE_GRAPH",
        "CUSTOM / ExampleCustomTextOnlyData / TEXT",
    };
    EXPECT_EQ(labels(items), expected);
    EXPECT_EQ(items.front().path, PERCENTAGE_TEXT);
}

/** @brief An element is present when its path exists in the text, and its line is the line of its key. */
TEST(ThemeItems, PresenceAndLine)
{
    const YAML::Node theme = parse(THEME_TEXT);
    EXPECT_TRUE(theme_items::is_present(theme, PERCENTAGE_TEXT));
    EXPECT_FALSE(theme_items::is_present(theme, PERCENTAGE_GRAPH));
    EXPECT_FALSE(theme_items::is_present(theme, {"STATS", "GPU", "MEMORY_PERCENT", "TEXT"}));
    EXPECT_EQ(theme_items::line_of(theme, PERCENTAGE_TEXT), 6);
    EXPECT_EQ(theme_items::line_of(theme, FREQUENCY_TEXT), 12);
    EXPECT_FALSE(theme_items::line_of(theme, PERCENTAGE_GRAPH).has_value());
}

/** @brief SHOW is changed in the text, the comment and the other lines are kept. */
TEST(ThemeItems, SetShow)
{
    std::string text = THEME_TEXT;
    EXPECT_TRUE(theme_items::set_show(text, PERCENTAGE_TEXT, false));
    EXPECT_NE(text.find("        SHOW: False  # shown on the display\n"), std::string::npos);
    EXPECT_EQ(text.find("SHOW: True"), std::string::npos);
    EXPECT_EQ(parse(text)["STATS"]["CPU"]["PERCENTAGE"]["TEXT"]["SHOW"].as<bool>(), false);

    EXPECT_TRUE(theme_items::set_show(text, PERCENTAGE_TEXT, true));
    EXPECT_NE(text.find("        SHOW: True  # shown on the display\n"), std::string::npos);

    // Already in the wanted state: nothing changes
    const std::string before = text;
    EXPECT_TRUE(theme_items::set_show(text, PERCENTAGE_TEXT, true));
    EXPECT_EQ(text, before);
}

/** @brief SHOW keeps the style of the text: lowercase values stay lowercase. */
TEST(ThemeItems, SetShowLowercase)
{
    std::string text = "STATS:\n  CPU:\n    PERCENTAGE:\n      TEXT:\n        SHOW: true\n";
    EXPECT_TRUE(theme_items::set_show(text, PERCENTAGE_TEXT, false));
    EXPECT_NE(text.find("SHOW: false\n"), std::string::npos);
}

/** @brief SHOW cannot be changed for an element that is not in the text, nor in an invalid text. */
TEST(ThemeItems, SetShowMissing)
{
    std::string text = THEME_TEXT;
    EXPECT_FALSE(theme_items::set_show(text, PERCENTAGE_GRAPH, true));
    EXPECT_EQ(text, THEME_TEXT);

    std::string invalid = "STATS: [unclosed\n";
    EXPECT_FALSE(theme_items::set_show(invalid, PERCENTAGE_TEXT, true));
}

/** @brief A missing form of an existing measure is added at the end of the measure, with default values and SHOW True. */
TEST(ThemeItems, AddFormOfExistingMeasure)
{
    std::string text = THEME_TEXT;
    EXPECT_TRUE(theme_items::add_item(text, parse(DEFAULT_THEME), PERCENTAGE_GRAPH));

    const YAML::Node theme = parse(text);
    const YAML::Node graph = theme["STATS"]["CPU"]["PERCENTAGE"]["GRAPH"];
    EXPECT_EQ(graph["SHOW"].as<bool>(), true);
    EXPECT_EQ(graph["X"].as<int>(), 0);
    EXPECT_EQ(graph["Y"].as<int>(), 0);

    // Added after the TEXT form of PERCENTAGE, before FREQUENCY: the lines after it move down by the 4 added lines
    EXPECT_EQ(theme_items::line_of(theme, PERCENTAGE_GRAPH), 10);
    EXPECT_EQ(theme_items::line_of(theme, FREQUENCY_TEXT), 16);
    // Existing content is kept, with its comment
    EXPECT_NE(text.find("# shown on the display"), std::string::npos);
    EXPECT_EQ(theme["STATS"]["CPU"]["FREQUENCY"]["TEXT"]["X"].as<int>(), 5);
}

/** @brief A missing measure is added in the section of its category, with its missing blocks. */
TEST(ThemeItems, AddMissingMeasure)
{
    std::string text = THEME_TEXT;
    const std::vector<std::string> path = {"STATS", "GPU", "MEMORY_PERCENT", "TEXT"};
    EXPECT_TRUE(theme_items::add_item(text, parse(DEFAULT_THEME), path));

    const YAML::Node theme = parse(text);
    EXPECT_TRUE(theme_items::is_present(theme, path));
    EXPECT_EQ(theme["STATS"]["GPU"]["MEMORY_PERCENT"]["TEXT"]["SHOW"].as<bool>(), true);
    // The GPU section keeps its interval
    EXPECT_EQ(theme["STATS"]["GPU"]["INTERVAL"].as<int>(), 1);
}

/** @brief A custom source missing from the text gets its block under STATS, with the default values of the forms. */
TEST(ThemeItems, AddCustomItem)
{
    std::string text = THEME_TEXT;
    const std::vector<std::string> path = {"STATS", "CUSTOM", "ExampleCustomNumericData", "GRAPH"};
    EXPECT_TRUE(theme_items::add_item(text, parse(DEFAULT_THEME), path));

    const YAML::Node theme = parse(text);
    EXPECT_TRUE(theme_items::is_present(theme, path));
    EXPECT_EQ(theme["STATS"]["CUSTOM"]["ExampleCustomNumericData"]["GRAPH"]["SHOW"].as<bool>(), true);
    EXPECT_EQ(theme["STATS"]["CPU"]["PERCENTAGE"]["TEXT"]["X"].as<int>(), 10);
}

/** @brief An element already in the text, or an invalid text, is not changed. */
TEST(ThemeItems, AddItemRefused)
{
    std::string text = THEME_TEXT;
    EXPECT_FALSE(theme_items::add_item(text, parse(DEFAULT_THEME), PERCENTAGE_TEXT));
    EXPECT_EQ(text, THEME_TEXT);

    std::string invalid = "STATS: [unclosed\n";
    EXPECT_FALSE(theme_items::add_item(invalid, parse(DEFAULT_THEME), PERCENTAGE_GRAPH));
}

/** @brief A text without STATS gets the block at its end. */
TEST(ThemeItems, AddItemWithoutStats)
{
    std::string text = "author: \"test\"\n";
    const std::vector<std::string> path = {"STATS", "CPU", "PERCENTAGE", "TEXT"};
    EXPECT_TRUE(theme_items::add_item(text, parse(DEFAULT_THEME), path));
    const YAML::Node theme = parse(text);
    EXPECT_EQ(theme["author"].as<std::string>(), "test");
    EXPECT_EQ(theme["STATS"]["CPU"]["PERCENTAGE"]["TEXT"]["SHOW"].as<bool>(), true);
}

/** @brief An element is shown when its SHOW value is True, not when it is False or absent. */
TEST(ThemeItems, IsShown)
{
    const YAML::Node theme = parse(THEME_TEXT);
    EXPECT_TRUE(theme_items::is_shown(theme, PERCENTAGE_TEXT));
    EXPECT_FALSE(theme_items::is_shown(theme, FREQUENCY_TEXT));
    EXPECT_FALSE(theme_items::is_shown(theme, PERCENTAGE_GRAPH));
    ASSERT_TRUE(theme_items::form_node(theme, PERCENTAGE_TEXT).has_value());
    EXPECT_EQ((*theme_items::form_node(theme, PERCENTAGE_TEXT))["X"].as<int>(), 10);
}

/**
 * @brief Every element of the list can be added to an empty theme: it is shown, at 0,0, with the keys of its default
 * block in res/themes/default.yaml. Custom sources use the forms of CPU/PERCENTAGE as default block.
 */
TEST(ThemeItems, AddEveryItemWithDefaultValues)
{
    // Repository root, whatever the directory the tests are run from
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
                                       .parent_path();
    const YAML::Node default_theme = YAML::LoadFile((root / "res/themes/default.yaml").string());
    const std::vector<theme_items::CustomSource> custom = {{"ExampleCustomNumericData", true},
                                                           {"ExampleCustomTextOnlyData", false}};
    const auto items = theme_items::available_items(default_theme, custom);
    ASSERT_FALSE(items.empty());

    for (const auto &item: items) {
        // Deprecated measures are not offered
        EXPECT_FALSE(item.path.size() > 2 && item.path[1] == "GPU" && item.path[2] == "MEMORY") << item.label;

        std::string text;
        ASSERT_TRUE(theme_items::add_item(text, default_theme, item.path)) << item.label;
        const YAML::Node theme = YAML::Load(text);
        ASSERT_TRUE(theme_items::is_shown(theme, item.path)) << item.label;

        const auto block = theme_items::form_node(theme, item.path);
        ASSERT_TRUE(block.has_value()) << item.label;
        EXPECT_EQ((*block)["X"].as<int>(), 0) << item.label;
        EXPECT_EQ((*block)["Y"].as<int>(), 0) << item.label;

        // Same keys as the default block of the form
        std::vector<std::string> default_path = item.path;
        if (item.path[1] == "CUSTOM") {
            default_path = {"STATS", "CPU", "PERCENTAGE", item.path.back()};
        }
        const YAML::Node default_block = YAML::Clone(default_theme);
        const auto default_form = theme_items::form_node(default_block, default_path);
        ASSERT_TRUE(default_form.has_value()) << item.label;
        std::set<std::string> expected, actual;
        for (const auto &entry: *default_form) {
            expected.insert(entry.first.as<std::string>());
        }
        for (const auto &entry: *block) {
            actual.insert(entry.first.as<std::string>());
        }
        EXPECT_EQ(actual, expected) << item.label;
    }
}
