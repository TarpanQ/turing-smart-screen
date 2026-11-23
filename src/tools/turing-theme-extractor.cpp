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

// C++ port of tools/turing-theme-extractor.py: Extract resources from a Turing Smart Screen theme (.data files) made
// for Windows app. The PNG can then be re-used to create a theme for System Monitor (see Wiki for theme creation)
// To run for all files of a folder:
// for file in "FOLDER"/*; do if [ -f "$file" ]; then turing-theme-extractor "$file"; fi; done

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::printf("Usage :\n");
        std::printf("        turing-theme-extractor path/to/theme-file.data\n");
        std::printf("Examples : \n");
        std::printf("        turing-theme-extractor \"Dragon Ball.data\"\n");
        std::printf("        turing-theme-extractor \"Pikachu theme.data\"\n");
        std::printf("        turing-theme-extractor NZXT_BLUR.data\n");
        return 0;
    }

    const std::string_view PNG_SIGNATURE("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);
    const std::string_view PNG_IEND("\x49\x45\x4E\x44\xAE\x42\x60\x82", 8);

    std::ifstream theme_file(argv[1], std::ios::binary);
    if (!theme_file) {
        std::fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string data((std::istreambuf_iterator<char>(theme_file)), std::istreambuf_iterator<char>());

    const std::string theme_name = fs::path(argv[1]).stem().string();
    fs::create_directories(theme_name);

    // Find PNG signature in binary data
    int found_png = 0;
    size_t header_found = data.find(PNG_SIGNATURE);
    while (header_found != std::string::npos) {
        std::printf("\nFound PNG header at 0x%06zx\n", header_found);

        // Find PNG IEND chunk (= end of file)
        const size_t iend_found = data.find(PNG_IEND, header_found);
        if (iend_found == std::string::npos) {
            std::printf("PNG end-of-file not found\n");
            break;
        }
        std::printf("Found PNG end-of-file at 0x%06zx\n", iend_found);

        // Extract PNG data to a file
        const std::string png_path = theme_name + "/" + std::to_string(header_found) + ".png";
        std::ofstream png_file(png_path, std::ios::binary);
        png_file.write(data.data() + header_found, static_cast<std::streamsize>(iend_found - header_found + PNG_IEND.size()));
        std::printf("PNG extracted to %s\n", png_path.c_str());
        found_png++;

        // Find next PNG signature (if any)
        header_found = data.find(PNG_SIGNATURE, iend_found);
    }

    std::printf("\n%d PNG files extracted from theme to current directory\n", found_png);
    return 0;
}
