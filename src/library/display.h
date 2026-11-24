// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// Copyright (C) 2021 Matthieu Houdebine (mathoudebine)
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
 * @file display.h
 * @brief One display of config.json (its settings, theme and LCD driver), with the static images and texts of its theme.
 */

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "library/config.h"
#include "library/lcd/lcd_comm.h"

/** @brief A display of the displays array of config.json: its settings, its theme and its LCD driver. */
class Display
{
public:
    /**
     * @brief Create the LCD driver selected by the revision, and load the theme. Throws std::runtime_error if the revision
     * is unknown or the driver cannot be opened. index is the position of the display in config.json: the first one shows the simulated screen on the web
     * server, the others save their screenshot in screencap-<index>.png.
     */
    Display(const config::DisplayConfig &display_settings, size_t index);

    /** @brief Load the theme of the display again, for example after the theme editor changed it. */
    void load_theme();

    /** @brief Initialize the display and clear it. */
    void initialize_display();
    /** @brief Turn the display on. */
    void turn_on();
    /** @brief Turn the display off. */
    void turn_off();
    /** @brief Draw the static images of the theme. */
    void display_static_images();
    /** @brief Draw the static texts of the theme. */
    void display_static_text();

    /** @brief Settings of the display, from the displays array of config.json. */
    config::DisplayConfig settings;
    /** @brief Theme of the display: theme.yaml with the default values. */
    YAML::Node theme;
    /** @brief Requests waiting to be sent to this display, sent by its own queue handler. */
    UpdateQueue update_queue;
    /** @brief LCD driver used by the display. */
    std::unique_ptr<LcdComm> lcd;
};

/** @brief Theme orientation, from the theme DISPLAY_ORIENTATION and the DISPLAY_REVERSE of the display */
Orientation get_theme_orientation(const config::DisplayConfig &settings, const YAML::Node &theme);

/** @brief Display (width, height) in portrait orientation, from the theme DISPLAY_SIZE */
std::pair<int, int> get_theme_size(const YAML::Node &theme);

/**
 * @brief The displays, one per element of the displays array of config.json, created on first use (config::init() must
 * have been called before)
 */
std::vector<std::unique_ptr<Display>> &displays();

/** @brief The first display (displays().front()) */
Display &display();
