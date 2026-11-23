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
 * @file stats.h
 * @brief Collects the metrics (CPU/GPU/RAM/disk/net/date/uptime/custom/weather/ping) and displays them with the styles of the theme.
 *
 * stats::init() must be called once, after config::init() and before using the other functions.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace stats {

/** @brief Default number of values kept for the line graphs. */
constexpr int DEFAULT_HISTORY_SIZE = 10;

/** @brief Select the sensors from HW_SENSORS in config.json. Exits the program if the value is not supported. */
void init();

/** @brief Full path of a file of the theme, std::nullopt if no name */
std::optional<std::string> get_theme_file_path(const std::string &name);

/**
 * @brief Shortest positive INTERVAL (seconds) of a stats item over the themes of all displays, 0 if none defines it.
 * @param path Path of the item in the theme, starting with "STATS" (for example {"STATS", "CPU", "PERCENTAGE"})
 */
double shared_interval(const std::vector<std::string> &path);

/**
 * @brief Largest HISTORY_SIZE of a line graph over the themes of all displays: the history is shared by the displays,
 * and each display shows the last values of its own HISTORY_SIZE.
 * @param path Path of the line graph in the theme, starting with "STATS"
 */
int shared_history_size(const std::vector<std::string> &path);

void display_themed_value(const YAML::Node &theme_data, const std::optional<std::string> &value, int min_size = 0,
                          const std::string &unit = "");
void display_themed_percent_value(const YAML::Node &theme_data, double value);
void display_themed_temperature_value(const YAML::Node &theme_data, double value);
void display_themed_progress_bar(const YAML::Node &theme_data, double value);

/**
 * @brief value: numeric value of the bar, value_text: value as text, displayed in the center of the bar if SHOW_TEXT
 * (custom_text replaces value_text + unit if provided)
 */
void display_themed_radial_bar(const YAML::Node &theme_data, double value, const std::string &value_text,
                               int min_size = 0, const std::string &unit = "",
                               const std::optional<std::string> &custom_text = std::nullopt);
void display_themed_percent_radial_bar(const YAML::Node &theme_data, double value);
void display_themed_temperature_radial_bar(const YAML::Node &theme_data, double value);
void display_themed_line_graph(const YAML::Node &theme_data, const std::vector<double> &values);

/** @brief Store the value in the list of last values (oldest value removed), used for line graphs */
void save_last_value(double value, std::vector<double> &last_values, int history_size);
std::vector<double> last_values_list(int size);

/** @brief Same as psutil bytes2human(): human-readable size, e.g. 1.2K, with the given std::format format for the value */
std::string bytes2human(double n, const std::string &value_format = "{:.1f}", const std::string &suffix = "");

/** @brief CPU statistics displayed by the theme. */
class CPU
{
public:
    /** @brief Display the CPU usage. */
    static void percentage();
    /** @brief Display the CPU frequency. */
    static void frequency();
    /** @brief Display the system load. */
    static void load();
    /** @brief Display the CPU temperature. */
    static void temperature();
    /** @brief Display the CPU fan speed. */
    static void fan_speed();
    /** @brief Display the CPU model name. */
    static void model();
};

/** @brief GPU statistics displayed by the theme. */
class Gpu
{
public:
    /** @brief Display the GPU statistics. */
    static void stats();
    /** @brief Display the GPU model name. */
    static void model();
    /** @brief True if the GPU statistics are available. */
    static bool is_available();
};

/** @brief Memory statistics displayed by the theme. */
class Memory
{
public:
    /** @brief Display the memory statistics. */
    static void stats();
    /** @brief Display the RAM module model. */
    static void model();
};

/** @brief Disk statistics displayed by the theme. */
class Disk
{
public:
    /** @brief Display the disk statistics. */
    static void stats();
};

/** @brief Network statistics displayed by the theme. */
class Net
{
public:
    /** @brief Display the network statistics. */
    static void stats();
};

/** @brief Date and time displayed by the theme. */
class Date
{
public:
    /** @brief Display the date and time. */
    static void stats();
};

/** @brief System uptime displayed by the theme. */
class SystemUptime
{
public:
    /** @brief Display the system uptime. */
    static void stats();
};

/** @brief Custom data sources displayed by the theme. */
class Custom
{
public:
    /** @brief Display the custom data sources. */
    static void stats();
};

/** @brief Weather displayed by the theme. */
class Weather
{
public:
    /** @brief Display the weather. */
    static void stats();
};

/** @brief Ping displayed by the theme. */
class Ping
{
public:
    /** @brief Display the ping result. */
    static void stats();
};

}  // namespace stats
