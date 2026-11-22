// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// Copyright (C) 2021 Matthieu Houdebine (mathoudebine)
// Copyright (C) 2022 Rollbacke
// Copyright (C) 2022 Ebag333
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
 * @file config.cpp
 */

#include "library/config.h"

#include <cstdlib>
#include <fstream>

#include "library/log.h"

namespace fs = std::filesystem;

namespace config {

fs::path MAIN_DIRECTORY;
std::string FONTS_DIR;
/** @brief Content of config.json. */
Json CONFIG_DATA = Json::object();
/** @brief Default values of the theme settings. */
YAML::Node THEME_DEFAULT;
/** @brief Content of the theme file (theme.yaml) in use. */
YAML::Node THEME_DATA;
std::vector<DisplayConfig> DISPLAYS;

namespace {

void write_json_file(const fs::path &path, const Json &data)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    out << data.dump(4) << "\n";
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

bool is_main_directory(const fs::path &dir)
{
    std::error_code ec;
    return fs::is_regular_file(dir / "config.json", ec) && fs::is_directory(dir / "res" / "themes", ec);
}

}  // namespace

fs::path find_main_directory()
{
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        for (fs::path dir = exe.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            if (is_main_directory(dir)) {
                return dir;
            }
            if (dir == dir.root_path()) {
                break;
            }
        }
    }
    return fs::current_path();
}

/** @brief Load a JSON configuration file. */
Json load_json(const fs::path &configfile)
{
    std::ifstream in(configfile);
    if (!in) {
        throw std::runtime_error("Cannot read " + configfile.string());
    }
    Json data = Json::parse(in);
    if (!data.is_object()) {
        throw std::runtime_error(configfile.string() + " does not contain a JSON object");
    }
    return data;
}

DisplayConfig DisplayConfig::from_json(const Json &node)
{
    DisplayConfig display;
    display.com_port = get(node, "COM_PORT", std::string("AUTO"));
    display.theme = node.at("THEME").get<std::string>();
    display.revision = node.at("REVISION").get<std::string>();
    display.brightness = node.at("BRIGHTNESS").get<int>();
    display.display_reverse = get(node, "DISPLAY_REVERSE", false);
    display.reset_on_startup = get(node, "RESET_ON_STARTUP", true);
    return display;
}

Json DisplayConfig::to_json() const
{
    return Json{{"COM_PORT", com_port},
                {"THEME", theme},
                {"REVISION", revision},
                {"BRIGHTNESS", brightness},
                {"DISPLAY_REVERSE", display_reverse},
                {"RESET_ON_STARTUP", reset_on_startup}};
}

std::vector<DisplayConfig> load_displays(const Json &root)
{
    const Json displays = root.is_object() && root.contains("displays") ? root.at("displays") : Json();
    if (!displays.is_array() || displays.empty()) {
        throw std::runtime_error("config.json must contain a non-empty \"displays\" array");
    }
    std::vector<DisplayConfig> result;
    for (const Json &node: displays) {
        result.push_back(DisplayConfig::from_json(node));
    }
    return result;
}

/** @brief Write the displays array of a configuration file, keeping the other sections. */
void save_displays(const fs::path &path, const std::vector<DisplayConfig> &displays)
{
    Json data = load_json(path);
    Json array = Json::array();
    for (const DisplayConfig &display: displays) {
        array.push_back(display.to_json());
    }
    data["displays"] = array;
    write_json_file(path, data);
}

/** @brief Load a YAML file (theme or default values). */
YAML::Node load_yaml(const fs::path &configfile)
{
    return YAML::LoadFile(configfile.string());
}

Json section(const Json &root, const std::string &name)
{
    if (root.is_object() && root.contains(name) && root.at(name).is_object()) {
        return root.at(name);
    }
    return Json::object();
}

void copy_default(const YAML::Node &default_values, YAML::Node theme)
{
    for (const auto &item: default_values) {
        const std::string key = item.first.as<std::string>();
        const YAML::Node &value = item.second;
        YAML::Node theme_value = theme[key];
        if (!theme_value.IsDefined() || theme_value.IsNull()) {
            theme[key] = YAML::Clone(value);
        } else if (value.IsMap() && theme_value.IsMap()) {
            copy_default(value, theme_value);
        }
    }
}

/** @brief Update the values of a configuration file, keeping the order of its keys. */
void update_json_file(const fs::path &path, const std::vector<ConfigUpdate> &updates)
{
    Json data = load_json(path);
    for (const auto &update: updates) {
        if (!data.contains(update.section) || !data.at(update.section).is_object()) {
            data[update.section] = Json::object();
        }
        data[update.section][update.key] = update.value;
    }
    write_json_file(path, data);
}

YAML::Node read_theme(const std::string &theme_name)
{
    const fs::path theme_path = fs::path("res/themes") / theme_name;
    YAML::Node theme_data = load_yaml(MAIN_DIRECTORY / theme_path / "theme.yaml");
    if (!theme_data.IsMap()) {
        throw std::runtime_error("theme.yaml is not a map");
    }
    theme_data["PATH"] = (MAIN_DIRECTORY / theme_path).string() + "/";
    copy_default(THEME_DEFAULT, theme_data);
    return theme_data;
}

void load_theme(bool exit_on_error)
{
    try {
        const std::string theme_name = DISPLAYS.front().theme;
        logger.info("Loading theme {} from {}", theme_name, (fs::path("res/themes") / theme_name / "theme.yaml").string());
        THEME_DATA = read_theme(theme_name);
    } catch (const std::exception &e) {
        logger.error("Theme not found or contains errors! ({})", e.what());
        if (exit_on_error) {
            std::_Exit(1);
        }
        throw std::runtime_error(std::string("Theme not found or contains errors: ") + e.what());
    }
}
void check_theme_compatible(const std::string &display_size)
{
    // Check if theme is compatible with hardware revision
    if (display_size != get(THEME_DATA["display"], "DISPLAY_SIZE", "3.5\"")) {
        logger.error("The selected theme {} is not compatible with your display revision {}",
                     DISPLAYS.front().theme, DISPLAYS.front().revision);
        std::_Exit(1);
    }
}

RGBColor get_color(const YAML::Node &node, const std::string &key, RGBColor default_value)
{
    if (!node.IsDefined() || !node.IsMap()) {
        return default_value;
    }
    const YAML::Node value = node[key];
    if (!value.IsDefined() || value.IsNull()) {
        return default_value;
    }
    return parse_color(value);
}

void init(bool with_theme)
{
    MAIN_DIRECTORY = find_main_directory();
    FONTS_DIR = (MAIN_DIRECTORY / "res" / "fonts").string() + "/";

    try {
        CONFIG_DATA = load_json(MAIN_DIRECTORY / "config.json");
        DISPLAYS = load_displays(CONFIG_DATA);
        THEME_DEFAULT = load_yaml(MAIN_DIRECTORY / "res/themes/default.yaml");
    } catch (const std::exception &e) {
        logger.error("Cannot load configuration from {}: {}", MAIN_DIRECTORY.string(), e.what());
        std::_Exit(1);
    }

    // Load theme of the first display, and check that the theme of every display is valid
    if (with_theme) {
        load_theme();
        for (const auto &display: DISPLAYS) {
            try {
                read_theme(display.theme);
            } catch (const std::exception &e) {
                logger.error("Theme {} not found or contains errors! ({})", display.theme, e.what());
                std::_Exit(1);
            }
        }
    }
}

}  // namespace config
