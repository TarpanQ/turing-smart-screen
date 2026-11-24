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
 * @file lcd_comm_rev_a.h
 * @brief This class is for Turing Smart Screen (rev. A) 3.5" and UsbMonitor screens (all sizes)
 */

#pragma once

#include "library/lcd/lcd_comm.h"

namespace rev_a {

/** @brief Commands of the revision A protocol. */
enum Command : uint8_t
{
    RESET = 101,  // Resets the display
    CLEAR = 102,  // Clears the display to a white screen
    TO_BLACK = 103,  // Makes the screen go black. NOT TESTED
    SCREEN_OFF = 108,  // Turns the screen off
    SCREEN_ON = 109,  // Turns the screen on
    SET_BRIGHTNESS = 110,  // Sets the screen brightness
    SET_ORIENTATION = 121,  // Sets the screen orientation
    DISPLAY_BITMAP = 197,  // Displays an image on the screen

    // Commands below are only supported by next generation Turing Smart screens
    LCD_28 = 40,  // ?
    LCD_29 = 41,  // ?
    HELLO = 69,  // Asks the screen for its model: 3.5", 5" or 7"
    SET_MIRROR = 122,  // Mirrors the rendering on the screen
    DISPLAY_PIXELS = 195,  // Displays a list of pixels than can be non-contiguous in one command, useful for line charts
};

/** @brief Sub-revisions of the revision A screens, they differ by their size. */
enum class SubRevision
{
    TURING_3_5,  // Official Turing 3.5 do not answer to HELLO command
    USBMONITOR_3_5,  // Answers 01 01 01 01 01 01 to HELLO
    USBMONITOR_5,  // Answers 02 02 02 02 02 02 to HELLO
    USBMONITOR_7,  // Answers 03 03 03 03 03 03 to HELLO
};

}  // namespace rev_a

/** @brief Driver for the Turing revision A screens (3.5 inch, UsbMonitor). */
class LcdCommRevA : public LcdComm
{
public:
    /** @brief Create the driver for the given COM port and screen size. */
    LcdCommRevA(const std::string &com_port = "AUTO", int display_width = 320, int display_height = 480,
                UpdateQueue *update_queue = nullptr);
    ~LcdCommRevA() override;

    std::optional<std::string> auto_detect_com_port() override;

    /** @brief Send a command with its four parameters to the screen. */
    void SendCommand(rev_a::Command cmd, int x, int y, int ex, int ey, bool bypass_queue = false);

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
    /** @brief Set the orientation of the screen. */
    void SetOrientation(Orientation orientation = Orientation::PORTRAIT) override;
    /** @brief Display an image at (x, y), cropped to image_width x image_height if given. */
    void DisplayPILImage(const Image &image, int x = 0, int y = 0, int image_width = 0, int image_height = 0) override;

    /** @brief Sub-revision of the screen, set by the revision A detection. */
    rev_a::SubRevision sub_revision = rev_a::SubRevision::TURING_3_5;

private:
    void _hello();
};
