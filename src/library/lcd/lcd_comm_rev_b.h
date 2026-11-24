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
 * @file lcd_comm_rev_b.h
 * @brief This class is for XuanFang (rev. B & flagship) 3.5" screens
 */

#pragma once

#include <vector>

#include "library/lcd/lcd_comm.h"
#include "library/lcd/serialize.h"

namespace rev_b {

/** @brief Commands of the revision B protocol. */
enum Command : uint8_t
{
    HELLO = 0xCA,  // Establish communication before driving the screen
    SET_ORIENTATION = 0xCB,  // Sets the screen orientation
    DISPLAY_BITMAP = 0xCC,  // Displays an image on the screen
    SET_LIGHTING = 0xCD,  // Sets the screen backplate RGB LED color
    SET_BRIGHTNESS = 0xCE,  // Sets the screen brightness
};

/**
 * @brief In revision B, basic orientations (portrait / landscape) are managed by the display
 * The reverse orientations (reverse portrait / reverse landscape) are software-managed
 */
enum OrientationValueRevB : uint8_t
{
    ORIENTATION_PORTRAIT = 0x0,
    ORIENTATION_LANDSCAPE = 0x1,
};

/** @brief HW revision B offers 4 sub-revisions to identify the HW capabilities */
enum class SubRevision
{
    A01 = 0xA01,  // HW revision B - brightness 0/1
    A02 = 0xA02,  // HW revision "flagship" - brightness 0/1
    A11 = 0xA11,  // HW revision B - brightness 0-255
    A12 = 0xA12,  // HW revision "flagship" - brightness 0-255
};

}  // namespace rev_b

/** @brief Driver for the XuanFang revision B and flagship screens. */
class LcdCommRevB : public LcdComm
{
public:
    /** @brief Create the driver for the given COM port and screen size. */
    LcdCommRevB(const std::string &com_port = "AUTO", int display_width = 320, int display_height = 480,
                UpdateQueue *update_queue = nullptr);
    ~LcdCommRevB() override;

    /** @brief True if the screen is a flagship model, with backplate LEDs. */
    bool is_flagship() const;
    /** @brief True if the brightness is set with the brightness range command. */
    bool is_brightness_range() const;

    std::optional<std::string> auto_detect_com_port() override;

    /** @brief New protocol (10 byte packets, framed with the command, 8 data bytes inside) */
    void SendCommand(rev_b::Command cmd, const std::vector<uint8_t> &payload = {}, bool bypass_queue = false);

    /** @brief Initialize the screen. */
    void InitializeComm() override;
    /** @brief Reset the screen. */
    void Reset() override;
    /** @brief Clear the screen. */
    void Clear() override;
    /** @brief Turn the screen off. */
    void ScreenOff() override;
    /** @brief Turn the screen on. */
    void ScreenOn() override;
    /** @brief Set the brightness, from 0 (off) to 100 (maximum). */
    void SetBrightness(int level = 25) override;
    /** @brief Set the color of the backplate LEDs, on flagship models. */
    void SetBackplateLedColor(RGBColor led_color = {255, 255, 255}) override;
    /** @brief Set the orientation of the screen. */
    void SetOrientation(Orientation orientation = Orientation::PORTRAIT) override;
    /** @brief Display an image at (x, y), cropped to image_width x image_height if given. */
    void DisplayPILImage(const Image &image, int x = 0, int y = 0, int image_width = 0, int image_height = 0) override;

    /** @brief Convert an image to the bytes sent to the screen. */
    Bytes serialize_image(const Image &image, int height, int width) const;

    rev_b::SubRevision sub_revision = rev_b::SubRevision::A01;  ///< Run a Hello command to detect correct sub-rev.

private:
    void _hello();
};
