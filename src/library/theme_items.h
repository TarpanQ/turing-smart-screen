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
 * @file theme_items.h
 * @brief Display elements of a theme (a stats measure and one of its forms, for example CPU/PERCENTAGE/TEXT): the list
 * of the available elements, and their presence, state (SHOW) and addition in the text of theme.yaml.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace theme_items {

/** @brief One display element of a theme: a form (TEXT, GRAPH, RADIAL, LINE_GRAPH...) of a stats measure. */
struct Item
{
    /** @brief Path in the theme, starting with "STATS", for example {"STATS", "CPU", "PERCENTAGE", "TEXT"}. */
    std::vector<std::string> path;
    /** @brief Name shown in the list, without STATS: "CPU / PERCENTAGE / TEXT". */
    std::string label;
};

/** @brief A custom data source (CUSTOM stats): its name, and whether it has numeric values (else only text). */
struct CustomSource
{
    /** @brief Name of the class, as used in the theme under STATS/CUSTOM. */
    std::string name;
    /** @brief True if the source has numeric values: it can be displayed as text, bar, radial or line graph. */
    bool numeric;
};

/**
 * @brief Every display element available: the forms of the stats of default_theme (theme.yaml with the default
 * values), and the forms of the custom data sources. Deprecated elements are not listed.
 */
std::vector<Item> available_items(const YAML::Node &default_theme, const std::vector<CustomSource> &custom_sources);

/** @brief True if the element is in the theme (its path exists in theme, the parsed text without default values). */
bool is_present(const YAML::Node &theme, const std::vector<std::string> &path);

/** @brief The block of the element in the theme, std::nullopt if the element is absent. */
std::optional<YAML::Node> form_node(const YAML::Node &theme, const std::vector<std::string> &path);

/** @brief True if the element is in the theme and its SHOW value is True. */
bool is_shown(const YAML::Node &theme, const std::vector<std::string> &path);

/**
 * @brief Line (0-based) where the element starts in the text of the theme (the line of its key), std::nullopt if the
 * element is not in the theme.
 */
std::optional<int> line_of(const YAML::Node &theme, const std::vector<std::string> &path);

/**
 * @brief Set the SHOW value of an element of the text, the other lines (including comments) are kept as they are.
 * @return False if the element or its SHOW value is not found, or if the text is not valid YAML.
 */
bool set_show(std::string &text, const std::vector<std::string> &path, bool show);

/**
 * @brief Add an element to the text, with the default values of its form in default_theme, SHOW True and X/Y 0. The
 * missing parent blocks are added too, at the end of the section of the closest existing parent. The text is changed
 * by lines, so comments are kept.
 * @return False if the element is already in the text, or if the text cannot be changed (not valid YAML, or the parent
 * is not a block).
 */
bool add_item(std::string &text, const YAML::Node &default_theme, const std::vector<std::string> &path);

}  // namespace theme_items
