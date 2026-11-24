// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
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
 * @file image.h
 * @brief RGB image and 2D drawing primitives, providing the subset of Pillow (PIL.Image / PIL.ImageDraw) needed by the display drivers.
 *
 * Coordinates and semantics follow Pillow so that the rendering stays close to Pillow: - boxes are (left, top, right, bottom), right/bottom excluded for crop(), included for drawing functions - angles are in degrees, 0 at 3 o'clock, increasing clockwise
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "library/lcd/color.h"

/** @brief RGB image, stored as a row-major array of pixels, with Pillow-like drawing functions. */
class Image
{
public:
    Image() = default;

    /** @brief New image filled with the given color (like Image.new('RGB', (width, height), color)) */
    Image(int width, int height, RGBColor color = {0, 0, 0});

    /**
     * @brief Load a PNG or JPEG file, converted to RGB (alpha channel dropped like Image.convert('RGB')).
     * Throws std::runtime_error if the file cannot be loaded.
     */
    static Image open(const std::string &path);

    /** @brief Save as PNG. Throws std::runtime_error on error. */
    void save(const std::string &path) const;

    /** @brief Width of the image, in pixels. */
    int width() const { return width_; }

    /** @brief Height of the image, in pixels. */
    int height() const { return height_; }

    /** @brief (width, height), like PIL Image.size */
    std::pair<int, int> size() const { return {width_, height_}; }

    /** @brief True if the image has no pixel. */
    bool empty() const { return width_ == 0 || height_ == 0; }

    /** @brief Packed RGB pixels, row by row (3 bytes per pixel) */
    const uint8_t *data() const { return pixels.data(); }

    /** @brief Color of the pixel at (x, y). */
    RGBColor getpixel(int x, int y) const;

    /** @brief Pixels out of the image are ignored */
    void putpixel(int x, int y, RGBColor color);

    /** @brief Blend the color over the pixel with the given opacity (0: transparent, 255: opaque) */
    void blend(int x, int y, RGBColor color, uint8_t alpha);

    /** @brief Part of the image in box [left, right) x [top, bottom). Areas outside the image are black (like PIL crop) */
    Image crop(int left, int top, int right, int bottom) const;

    /** @brief Resized copy, using a bicubic filter (Pillow default) */
    Image resize(int new_width, int new_height) const;

    /** @brief Copy the image with its top-left corner at (x, y). Parts out of this image are ignored */
    void paste(const Image &image, int x, int y);

    /** @brief Rotations that can be applied to an image. */
    enum class Transpose { ROTATE_90, ROTATE_180, ROTATE_270 };

    /** @brief Rotated copy, counter-clockwise (like PIL Image.transpose) */
    Image transpose(Transpose method) const;

    /**
     * @brief Rotated copy by a multiple of 90 degrees counter-clockwise, the size is swapped for 90/270 (like PIL
     * Image.rotate(angle, expand=True)). Throws std::invalid_argument for other angles.
     */
    Image rotate(int angle) const;

    /** @brief PNG file content. Throws std::runtime_error on error. */
    std::vector<uint8_t> encode_png() const;

    /** @brief JPEG file content, quality 1-100, chroma subsampling: 0 (4:4:4), 1 (4:2:2) or 2 (4:2:0), like PIL */
    std::vector<uint8_t> encode_jpeg(int quality, int subsampling) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels;
};

/** @brief Point with floating-point coordinates. */
struct PointF
{
    /** @brief X coordinate. */
    double x;
    /** @brief Y coordinate. */
    double y;
};

/** @brief Drawing primitives on an image (subset of PIL.ImageDraw) */
class ImageDraw
{
public:
    /** @brief Drawing helper for an image. */
    explicit ImageDraw(Image &image) : image(image) {}

    /** @brief Rectangle including both corners */
    void rectangle(double x0, double y0, double x1, double y1, std::optional<RGBColor> fill,
                   std::optional<RGBColor> outline, int width = 1);

    /** @brief Polyline joining all points */
    void line(const std::vector<PointF> &points, RGBColor fill, int width = 1);

    /** @brief Arc of the ellipse inscribed in the box, from start to end angle */
    void arc(double x0, double y0, double x1, double y1, double start, double end, RGBColor fill, int width = 1);

    /** @brief Ellipse inscribed in the box */
    void ellipse(double x0, double y0, double x1, double y1, std::optional<RGBColor> fill,
                 std::optional<RGBColor> outline, int width = 1);

    /** @brief Filled polygon */
    void polygon(const std::vector<PointF> &points, RGBColor fill);

private:
    void draw_line_1px(int x0, int y0, int x1, int y1, RGBColor color);

    /** @brief Fill pixels of the box whose center is inside the elliptic ring [inner, outer], within the angle range */
    void ellipse_ring(double x0, double y0, double x1, double y1, int width, bool full_ellipse, double start,
                      double end, RGBColor color);

    Image &image;
};
