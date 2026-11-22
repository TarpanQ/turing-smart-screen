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
 * @file compare-images.cpp
 * @brief Compare 2 pictures, and save their difference if they are not identical Usage: compare-images `picture1` `picture2` `difference output picture`
 */

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "library/lcd/image.h"

/** @brief Entry point of the compare-images tool. */
int main(int argc, char *argv[])
{
    if (argc < 4) {
        std::fprintf(stderr, "Usage: %s <picture1> <picture2> <difference output picture>\n", argv[0]);
        return 0;
    }

    Image im1, im2;
    try {
        im1 = Image::open(argv[1]);
    } catch (const std::exception &) {
        return 0;
    }
    try {
        im2 = Image::open(argv[2]);
    } catch (const std::exception &) {
        return 0;
    }

    const bool same_size = im1.size() == im2.size();
    bool identical = same_size;
    for (int y = 0; identical && y < im1.height(); y++) {
        for (int x = 0; identical && x < im1.width(); x++) {
            identical = im1.getpixel(x, y) == im2.getpixel(x, y);
        }
    }

    if (identical) {
        std::printf("The 2 pictures are visually identical\n");
        return 0;
    }

    std::printf("The 2 pictures are different!\n");
    // Absolute difference of each channel, on the common area (like PIL ImageChops.difference)
    const int width = std::min(im1.width(), im2.width());
    const int height = std::min(im1.height(), im2.height());
    Image diff(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const RGBColor a = im1.getpixel(x, y), b = im2.getpixel(x, y);
            diff.putpixel(x, y, {static_cast<uint8_t>(std::abs(a.r - b.r)), static_cast<uint8_t>(std::abs(a.g - b.g)),
                                 static_cast<uint8_t>(std::abs(a.b - b.b))});
        }
    }
    try {
        diff.save(argv[3]);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
