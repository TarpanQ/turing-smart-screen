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
UpdateQueue update_queue;

namespace {

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

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    out << data.dump(4) << "\n";
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

void load_theme(bool exit_on_error)
{
    try {
        const std::string theme_name = CONFIG_DATA.at("config").at("THEME").get<std::string>();
        const fs::path theme_path = fs::path("res/themes") / theme_name;
        logger.info("Loading theme {} from {}", theme_name, (theme_path / "theme.yaml").string());
        YAML::Node theme_data = load_yaml(MAIN_DIRECTORY / theme_path / "theme.yaml");
        if (!theme_data.IsMap()) {
            throw std::runtime_error("theme.yaml is not a map");
        }
        theme_data["PATH"] = (MAIN_DIRECTORY / theme_path).string() + "/";
        THEME_DATA = theme_data;
    } catch (const std::exception &e) {
        logger.error("Theme not found or contains errors! ({})", e.what());
        if (exit_on_error) {
            std::_Exit(1);
        }
        throw std::runtime_error(std::string("Theme not found or contains errors: ") + e.what());
    }

    copy_default(THEME_DEFAULT, THEME_DATA);
}

void check_theme_compatible(const std::string &display_size)
{
    // Check if theme is compatible with hardware revision
    if (display_size != get(THEME_DATA["display"], "DISPLAY_SIZE", "3.5\"")) {
        logger.error("The selected theme {} is not compatible with your display revision {}",
                     CONFIG_DATA.at("config").at("THEME").get<std::string>(),
                     CONFIG_DATA.at("display").at("REVISION").get<std::string>());
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
        THEME_DEFAULT = load_yaml(MAIN_DIRECTORY / "res/themes/default.yaml");
    } catch (const std::exception &e) {
        logger.error("Cannot load configuration from {}: {}", MAIN_DIRECTORY.string(), e.what());
        std::_Exit(1);
    }

    // Load theme
    if (with_theme) {
        load_theme();
    }
}

}  // namespace config
