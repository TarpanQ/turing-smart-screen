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
 * @file theme_items.cpp
 */

#include "library/theme_items.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace theme_items {

namespace {

// Forms of a custom data source with numeric values, and of a text-only source
const std::vector<std::string> NUMERIC_FORMS = {"TEXT", "GRAPH", "RADIAL", "LINE_GRAPH"};
const std::vector<std::string> TEXT_FORMS = {"TEXT"};

// Measure whose forms give the default values of the custom data sources (they have no default block)
const std::vector<std::string> CUSTOM_TEMPLATE = {"STATS", "CPU", "PERCENTAGE"};

// Measures kept for compatibility only: not offered in the list (GPU/MEMORY is replaced by MEMORY_PERCENT and others)
const std::vector<std::vector<std::string>> DEPRECATED = {{"GPU", "MEMORY"}};

// Node at path from node (const access: a missing key is never created)
std::optional<YAML::Node> find(const YAML::Node &node, const std::vector<std::string> &path, size_t index = 0)
{
    if (index == path.size()) {
        return node;
    }
    if (!node.IsMap()) {
        return std::nullopt;
    }
    const YAML::Node child = node[path[index]];
    if (!child.IsDefined()) {
        return std::nullopt;
    }
    return find(child, path, index + 1);
}

// Position of the key in its map
std::optional<YAML::Mark> key_mark(const YAML::Node &map, const std::string &key)
{
    for (const auto &entry: map) {
        if (entry.first.IsScalar() && entry.first.Scalar() == key) {
            return entry.first.Mark();
        }
    }
    return std::nullopt;
}

// True if path (starting with STATS) is under a deprecated measure
bool is_deprecated(const std::vector<std::string> &path)
{
    for (const auto &deprecated: DEPRECATED) {
        if (path.size() > deprecated.size() &&
            std::equal(deprecated.begin(), deprecated.end(), path.begin() + 1)) {
            return true;
        }
    }
    return false;
}

// Walks the default theme and adds each map with a SHOW key (a form) to items
void collect(const YAML::Node &node, std::vector<std::string> &path, std::vector<Item> &items)
{
    if (!node.IsMap()) {
        return;
    }
    for (const auto &entry: node) {
        if (!entry.first.IsScalar() || !entry.second.IsMap()) {
            continue;
        }
        const std::string key = entry.first.Scalar();
        if (key == "INTERVAL") {
            continue;
        }
        path.push_back(key);
        if (entry.second["SHOW"].IsDefined()) {
            if (!is_deprecated(path)) {
                std::string label;
                for (size_t i = 1; i < path.size(); i++) {
                    label += (i > 1 ? " / " : "") + path[i];
                }
                items.push_back({path, label});
            }
        } else {
            collect(entry.second, path, items);
        }
        path.pop_back();
    }
}

// Default values of a form: the form in default_theme, or the form of the custom template when the form is missing
YAML::Node form_template(const YAML::Node &default_theme, const std::vector<std::string> &path)
{
    if (const auto node = find(default_theme, path)) {
        return *node;
    }
    std::vector<std::string> fallback = CUSTOM_TEMPLATE;
    fallback.push_back(path.back());
    if (const auto node = find(default_theme, fallback)) {
        return *node;
    }
    return YAML::Node(YAML::NodeType::Map);
}

// Lines of a text, without the line feeds: join(split(text)) == text
std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t pos = text.find('\n'); pos != std::string::npos; pos = text.find('\n', start)) {
        lines.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
}

std::string join_lines(const std::vector<std::string> &lines)
{
    std::string text;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) {
            text += '\n';
        }
        text += lines[i];
    }
    return text;
}

std::string lower(std::string s)
{
    for (char &c: s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

size_t indent_of(const std::string &line)
{
    size_t count = 0;
    while (count < line.size() && line[count] == ' ') {
        count++;
    }
    return count;
}

bool is_blank_or_comment(const std::string &line)
{
    const size_t indent = indent_of(line);
    return indent == line.size() || line[indent] == '#';
}

// Parsed text, or std::nullopt if it is not valid YAML
std::optional<YAML::Node> parse(const std::string &text)
{
    try {
        return YAML::Load(text);
    } catch (const YAML::Exception &) {
        return std::nullopt;
    }
}

}  // namespace

std::vector<Item> available_items(const YAML::Node &default_theme, const std::vector<CustomSource> &custom_sources)
{
    std::vector<Item> items;
    std::vector<std::string> path = {"STATS"};
    if (const auto stats = find(default_theme, {"STATS"})) {
        collect(*stats, path, items);
    }

    for (const auto &source: custom_sources) {
        const auto &forms = source.numeric ? NUMERIC_FORMS : TEXT_FORMS;
        for (const auto &form: forms) {
            const std::vector<std::string> item_path = {"STATS", "CUSTOM", source.name, form};
            items.push_back({item_path, "CUSTOM / " + source.name + " / " + form});
        }
    }
    return items;
}

bool is_present(const YAML::Node &theme, const std::vector<std::string> &path)
{
    return find(theme, path).has_value();
}

std::optional<YAML::Node> form_node(const YAML::Node &theme, const std::vector<std::string> &path)
{
    return find(theme, path);
}

bool is_shown(const YAML::Node &theme, const std::vector<std::string> &path)
{
    const auto node = find(theme, path);
    if (!node || !node->IsMap()) {
        return false;
    }
    const YAML::Node &block = *node;
    const YAML::Node show = block["SHOW"];
    return show.IsScalar() && show.as<bool>(false);
}

std::optional<int> line_of(const YAML::Node &theme, const std::vector<std::string> &path)
{
    if (path.empty() || !is_present(theme, path)) {
        return std::nullopt;
    }
    const std::vector<std::string> parent(path.begin(), path.end() - 1);
    const auto parent_node = find(theme, parent);
    if (!parent_node) {
        return std::nullopt;
    }
    const auto mark = key_mark(*parent_node, path.back());
    if (!mark) {
        return std::nullopt;
    }
    return static_cast<int>(mark->line);
}

bool set_show(std::string &text, const std::vector<std::string> &path, bool show)
{
    const auto root = parse(text);
    if (!root) {
        return false;
    }
    const auto form = find(*root, path);
    if (!form || !form->IsMap()) {
        return false;
    }
    for (const auto &entry: *form) {
        if (!entry.first.IsScalar() || entry.first.Scalar() != "SHOW") {
            continue;
        }
        if (!entry.second.IsScalar()) {
            return false;
        }
        const auto line_index = static_cast<size_t>(entry.second.Mark().line);
        const auto column = static_cast<size_t>(entry.second.Mark().column);
        std::vector<std::string> lines = split_lines(text);
        if (line_index >= lines.size()) {
            return false;
        }
        std::string &line = lines[line_index];
        // The value is the word starting at its column, up to a space or a comment
        size_t end = column;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])) && line[end] != '#') {
            end++;
        }
        if (end == column) {
            return false;
        }
        const std::string current = line.substr(column, end - column);
        // Same style as the current value: lowercase (true/false) or capitalized (True/False)
        const bool lowercase = std::islower(static_cast<unsigned char>(current[0])) != 0;
        std::string value = show ? "True" : "False";
        if (lowercase) {
            value = show ? "true" : "false";
        }
        if (lower(current) == lower(value)) {
            return true;
        }
        line.replace(column, end - column, value);
        text = join_lines(lines);
        return true;
    }
    return false;
}

bool add_item(std::string &text, const YAML::Node &default_theme, const std::vector<std::string> &path)
{
    if (path.empty()) {
        return false;
    }
    const auto root = parse(text);
    if (!root || is_present(*root, path)) {
        return false;
    }

    // Closest existing parent: the path before the first missing key
    size_t depth = path.size() - 1;
    while (depth > 0 && !is_present(*root, std::vector<std::string>(path.begin(), path.begin() + depth))) {
        depth--;
    }

    // The form, with the default values of the element, and the missing blocks above it
    YAML::Node form = YAML::Clone(form_template(default_theme, path));
    form["SHOW"] = true;
    form["X"] = 0;
    form["Y"] = 0;
    YAML::Node generated = form;
    for (size_t i = path.size(); i-- > depth;) {
        YAML::Node wrapper;
        wrapper[path[i]] = generated;
        generated = wrapper;
    }
    YAML::Emitter emitter;
    emitter.SetIndent(2);
    emitter << generated;
    const std::vector<std::string> generated_lines = split_lines(emitter.c_str());

    std::vector<std::string> lines = split_lines(text);
    if (depth == 0) {
        // No section at all: the block is added at the end of the text, as a top-level key
        if (!lines.empty() && lines.back().empty()) {
            lines.pop_back();
        }
        for (const auto &line: generated_lines) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        text = join_lines(lines) + "\n";
        return true;
    }

    const std::vector<std::string> parent_path(path.begin(), path.begin() + depth);
    const auto parent = find(*root, parent_path);
    if (!parent || !parent->IsMap()) {
        return false;
    }
    const auto grandparent = find(*root, std::vector<std::string>(path.begin(), path.begin() + depth - 1));
    if (!grandparent) {
        return false;
    }
    const auto mark = key_mark(*grandparent, parent_path.back());
    if (!mark) {
        return false;
    }
    const size_t parent_column = mark->column;
    const size_t child_column = parent->size() > 0 ? parent->begin()->first.Mark().column : parent_column + 2;

    // The section of the parent ends before the next line at the same indentation or less
    size_t last_content = mark->line;
    for (size_t line = mark->line + 1; line < lines.size(); line++) {
        if (is_blank_or_comment(lines[line])) {
            continue;
        }
        if (indent_of(lines[line]) <= parent_column) {
            break;
        }
        last_content = line;
    }

    std::vector<std::string> indented;
    for (const auto &line: generated_lines) {
        indented.push_back(line.empty() ? line : std::string(child_column, ' ') + line);
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(last_content + 1), indented.begin(), indented.end());
    text = join_lines(lines);
    return true;
}

}  // namespace theme_items
