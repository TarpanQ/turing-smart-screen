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
 * @file config.h
 * @brief Loads config.json, then the theme res/themes/`THEME`/theme.yaml with fallback to the default values of
 * res/themes/default.yaml.
 *
 * config::init() must be called once at program start, before using anything else from the library.
 */

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "library/lcd/lcd_comm.h"

namespace config {

/** @brief JSON document of the configuration file (config.json). Keeps the order of the keys of the file. */
using Json = nlohmann::ordered_json;

/**
 * @brief Settings of one display: one element of the "displays" array of config.json.
 * Keys that are required (THEME, REVISION, BRIGHTNESS) make the program stop when they are missing.
 */
struct DisplayConfig
{
    /** @brief Serial port of the display, or AUTO for auto-detection. */
    std::string com_port = "AUTO";
    /** @brief Name of the theme folder in res/themes. */
    std::string theme;
    /** @brief Hardware revision of the display (A, B, C, D, TUR_USB, WEACT_A, WEACT_B or SIMU). */
    std::string revision = "A";
    /** @brief Brightness, from 0 to 100. */
    int brightness = 50;
    /** @brief Reverse the orientation of the display. */
    bool display_reverse = false;
    /** @brief Reset the display when the program starts. */
    bool reset_on_startup = true;

    /** @brief Read a display from an element of the "displays" array. Throws if a required key is missing. */
    static DisplayConfig from_json(const Json &node);
    /** @brief The display as an element of the "displays" array. */
    Json to_json() const;
};

/** @brief Directory containing config.json and res/ (the repository root) */
extern std::filesystem::path MAIN_DIRECTORY;
/** @brief Fonts directory, ending with '/' */
extern std::string FONTS_DIR;
/** @brief Content of config.json. */
extern Json CONFIG_DATA;
/** @brief Settings of the displays, one element per display of the "displays" array of config.json. */
extern std::vector<DisplayConfig> DISPLAYS;
/** @brief Default values of the theme settings. */
extern YAML::Node THEME_DEFAULT;
/** @brief Content of the theme file (theme.yaml) in use. */
extern YAML::Node THEME_DATA;

/** @brief Queue containing the serial requests to send to the screen */
extern UpdateQueue update_queue;

/**
 * @brief Directory containing config.json and res/: the executable directory or one of its parents (programs are built
 * into build/bin/), otherwise the current directory
 */
std::filesystem::path find_main_directory();

/**
 * @brief Load a JSON configuration file.
 * Throws std::runtime_error if the file cannot be read or is not a JSON object, nlohmann::json::exception if it is not
 * valid JSON.
 */
Json load_json(const std::filesystem::path &configfile);

/** @brief Load a YAML file (theme or default values), throws YAML::Exception if it cannot be loaded or parsed. */
YAML::Node load_yaml(const std::filesystem::path &configfile);

/**
 * @brief Read the "displays" array of a configuration. Throws std::runtime_error if it is missing or empty.
 */
std::vector<DisplayConfig> load_displays(const Json &root);

/**
 * @brief Write the "displays" array of a configuration file, replacing the displays of the file by displays.
 * Throws std::runtime_error if the file cannot be read or written.
 */
void save_displays(const std::filesystem::path &path, const std::vector<DisplayConfig> &displays);

/**
 * @brief Section of the configuration (for example "config"), or an empty object if the root has no such object.
 * Use it instead of operator[] on a const Json, which is undefined for missing keys.
 */
Json section(const Json &root, const std::string &name);

/** @brief Recursively supply default values into a map of maps of maps ... */
void copy_default(const YAML::Node &default_values, YAML::Node theme);

/** @brief A value to write in config.json: section (for example "config"), key and new value. */
struct ConfigUpdate
{
    /** @brief Section of the value to update. */
    std::string section;
    /** @brief Key of the value to update. */
    std::string key;
    /** @brief New value of the key. */
    Json value;
};

/**
 * @brief Update the values of a configuration file. Missing keys are added at the end of their section, missing sections
 * at the end of the file. The file is rewritten with 4-space indentation.
 * Throws std::runtime_error if the file cannot be read or written.
 */
void update_json_file(const std::filesystem::path &path, const std::vector<ConfigUpdate> &updates);

/**
 * @brief Load the theme selected in config.json into THEME_DATA. On error, exits the program if exit_on_error is true,
 * otherwise throws std::runtime_error (THEME_DATA is then unchanged if the theme file cannot be read or parsed).
 */
void load_theme(bool exit_on_error = true);

/** @brief Check if theme is compatible with hardware revision. Exits the program if not. */
void check_theme_compatible(const std::string &display_size);

/** @brief Load config.json, default.yaml and the theme (if with_theme is true). Exits the program on error. */
void init(bool with_theme = true);

/**
 * @brief Convert a JSON value to T. Strings also accept numbers and booleans, written as they are in the file.
 * Throws std::runtime_error if the value cannot be converted to T.
 */
template<typename T>
T json_as(const Json &value)
{
    if constexpr (std::is_same_v<T, std::string>) {
        if (value.is_string()) {
            return value.get<std::string>();
        }
        if (value.is_number() || value.is_boolean()) {
            return value.dump();
        }
        throw std::runtime_error("expected a string, got " + value.dump());
    } else {
        return value.get<T>();
    }
}

/**
 * @brief Value of node[key] converted to T, or default_value if the key is missing or null.
 * Throws std::runtime_error if the value cannot be converted to T.
 */
template<typename T>
T get(const Json &node, const std::string &key, const T &default_value)
{
    if (!node.is_object() || !node.contains(key)) {
        return default_value;
    }
    const Json &value = node.at(key);
    if (value.is_null()) {
        return default_value;
    }
    try {
        return json_as<T>(value);
    } catch (const std::exception &) {
        throw std::runtime_error("Invalid value for '" + key + "': '" + value.dump() + "'");
    }
}

/** @brief Overload so that string literals can be used as default value */
inline std::string get(const Json &node, const std::string &key, const char *default_value)
{
    return get<std::string>(node, key, std::string(default_value));
}

/**
 * @brief Value of node[key] converted to T, or default_value if the key is missing or empty.
 * Throws std::runtime_error if the value cannot be converted to T.
 */
template<typename T>
T get(const YAML::Node &node, const std::string &key, const T &default_value)
{
    if (!node.IsDefined() || !node.IsMap()) {
        return default_value;
    }
    const YAML::Node value = node[key];
    if (!value.IsDefined() || value.IsNull()) {
        return default_value;
    }
    try {
        return value.as<T>();
    } catch (const YAML::Exception &) {
        throw std::runtime_error("Invalid value for '" + key + "': '" + YAML::Dump(value) + "'");
    }
}

/**
 * @brief Color value of node[key] (see parse_color()), or default_value if the key is missing or empty.
 * Throws std::invalid_argument if the color cannot be parsed.
 */
RGBColor get_color(const YAML::Node &node, const std::string &key, RGBColor default_value);

/** @brief Overload so that string literals can be used as default value */
inline std::string get(const YAML::Node &node, const std::string &key, const char *default_value)
{
    return get<std::string>(node, key, std::string(default_value));
}

}  // namespace config
