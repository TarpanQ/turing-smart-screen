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
 * @file lcd_comm_rev_d.h
 * @brief This class is for Kipye Qiye Smart Display 3.5"
 */

#pragma once

#include "library/lcd/lcd_comm.h"
#include "library/lcd/serialize.h"

namespace rev_d {

/** @brief Commands of the revision D protocol. */
enum class Command
{
    GETINFO,      // 71 00 00 00
    SETORG,       // Set portrait orientation
    SET180,       // Set reverse portrait orientation
    SETHF,        // Set portrait orientation with horizontal mirroring
    SETVF,        // Set reverse portrait orientation with horizontal mirroring
    SETBL,        // Brightness setting
    DISPCOLOR,    // Display RGB565 color on whole screen
    BLOCKWRITE,   // Send bitmap size
    INTOPICMODE,  // Start bitmap transmission
    OUTPICMODE,   // End bitmap transmission
};

const Bytes &command_value(Command cmd);

}  // namespace rev_d

/** @brief Driver for the Kipye revision D screens (serial). */
class LcdCommRevD : public LcdComm
{
public:
    /** @brief Create the driver for the given COM port and screen size. */
    LcdCommRevD(const std::string &com_port = "AUTO", int display_width = 320, int display_height = 480,
                UpdateQueue *update_queue = nullptr);
    ~LcdCommRevD() override;

    std::optional<std::string> auto_detect_com_port() override;

    /** @brief Empty the input buffer after each write: we don't process acknowledgements the screen sends back */
    void WriteDataAndFlush(const Bytes &byteBuffer);

    /** @brief Send a command with an optional payload to the screen. */
    void SendCommand(rev_d::Command cmd, const Bytes &payload = {}, bool bypass_queue = false);

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
};
