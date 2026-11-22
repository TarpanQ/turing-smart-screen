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
 * @file theme-preview-generator.cpp
 * @brief Generate res/themes/themes.md, the list of themes with their preview
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "library/config.h"

namespace fs = std::filesystem;

namespace {

std::vector<std::string> get_themes(const fs::path &directory, const std::string &display_size)
{
    std::vector<std::string> themes;
    std::error_code ec;
    for (const auto &entry: fs::directory_iterator(directory, ec)) {
        // Check if a theme.yaml file exists
        const fs::path theme = entry.path() / "theme.yaml";
        if (!entry.is_directory() || !fs::is_regular_file(theme)) {
            continue;
        }
        // Get display size from theme.yaml
        try {
            const YAML::Node theme_data = YAML::LoadFile(theme.string());
            if (config::get(theme_data["display"], "DISPLAY_SIZE", "3.5\"") == display_size) {
                themes.push_back(entry.path().filename().string());
            }
        } catch (const YAML::Exception &) {
        }
    }
    std::sort(themes.begin(), themes.end(), [](const std::string &a, const std::string &b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
            return std::tolower(x) < std::tolower(y);
        });
    });
    return themes;
}

void write_theme_previews_to_file(const std::vector<std::string> &themes, std::ofstream &file, const std::string &size)
{
    file << "\n## " << size << " themes\n";
    file << "<table>";
    int i = 0;
    for (const auto &theme: themes) {
        file << "<td>" << theme
             << "<br/><img src=\"https://raw.githubusercontent.com/mathoudebine/turing-smart-screen-python/main/res/themes/"
             << theme << "/preview.png\" width=\"150\"/></td>";
        i = i + 1;
        if (i >= 5) {
            file << "</table><table>";
            i = 0;
        }
    }
    file << "</table>\n";
}

}  // namespace

/** @brief Entry point of the theme-preview-generator tool. */
int main()
{
    const fs::path themes_dir = config::find_main_directory() / "res" / "themes";
    const auto themes21inch = get_themes(themes_dir, "2.1\"");
    const auto themes3inch = get_themes(themes_dir, "3.5\"");
    const auto themes5inch = get_themes(themes_dir, "5\"");
    const auto themes88inch = get_themes(themes_dir, "8.8\"");

    std::ofstream file(themes_dir / "themes.md");
    file << "<!--- This file is generated automatically by GitHub Actions, do not edit it! --->\n";
    file << "\n";
    file << "# Turing Smart Screen themes\n";
    file << "\n";
    file << "ℹ️ Click on a preview to view full size\n\n";
    file << "[2.1\" themes](#21-themes)\n\n";
    file << "[3.5\" themes](#35-themes)\n\n";
    file << "[5\" themes](#5-themes)\n\n";
    file << "[8.8\" themes](#88-themes)\n";

    write_theme_previews_to_file(themes21inch, file, "2.1\"");
    write_theme_previews_to_file(themes3inch, file, "3.5\"");
    write_theme_previews_to_file(themes5inch, file, "5\"");
    write_theme_previews_to_file(themes88inch, file, "8.8\"");
    return file ? 0 : 1;
}
