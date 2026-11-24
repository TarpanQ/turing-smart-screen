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
 * @file font-preview.cpp
 * @brief Generate a preview picture `font`_preview.png for each font of res/fonts
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "library/config.h"
#include "library/lcd/font.h"
#include "library/lcd/image.h"

namespace fs = std::filesystem;

namespace {

void generate_preview(const fs::path &font_dir, const std::string &font_filename, const std::string &text,
                      int font_size = 40, RGBColor font_color = {0, 0, 0},
                      RGBColor background_color = {255, 255, 255})
{
    const Font ttfont((font_dir / font_filename).string(), font_size);
    Image text_image(1000, 1000, background_color);

    const TextBox box = ttfont.textbbox(0, 0, text);
    // textbbox may return float values, which is not good for the bitmap operations below.
    // Let's extend the bounding box to the next whole pixel in all directions
    const int left = static_cast<int>(std::floor(box.left)), top = static_cast<int>(std::floor(box.top));
    const int right = static_cast<int>(std::ceil(box.right)), bottom = static_cast<int>(std::ceil(box.bottom));

    ttfont.draw(text_image, 0, 0, text, font_color);

    text_image = text_image.crop(left, top, right, bottom);
    text_image.save((font_dir / (font_filename.substr(0, font_filename.size() - 4) + "_preview.png")).string());
}

std::vector<fs::path> sorted_entries(const fs::path &dir)
{
    std::vector<fs::path> entries;
    std::error_code ec;
    for (const auto &entry: fs::directory_iterator(dir, ec)) {
        entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

}  // namespace

/** @brief Entry point of the font-preview tool. */
int main()
{
    const fs::path fonts_dir = config::find_main_directory() / "res" / "fonts";
    for (const auto &font_dir: sorted_entries(fonts_dir)) {
        if (!fs::is_directory(font_dir)) {
            continue;
        }
        for (const auto &font_file: sorted_entries(font_dir)) {
            const std::string ext = font_file.extension().string();
            if (fs::is_regular_file(font_file) && (ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF")) {
                std::printf("Found font %s/%s\n", font_dir.filename().c_str(), font_file.filename().c_str());
                try {
                    generate_preview(font_dir, font_file.filename().string(), "40%", 60);
                } catch (const std::exception &e) {
                    std::fprintf(stderr, "Cannot generate preview: %s\n", e.what());
                }
            }
        }
    }
    return 0;
}
