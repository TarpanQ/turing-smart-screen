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
 * @file lcd_comm_weact_a.h
 * @brief This class is for WeAct Studio Display FS V1 3.5"
 */

#pragma once

#include <utility>

#include "library/lcd/lcd_comm.h"
#include "library/lcd/serialize.h"

namespace weact_a {

/** @brief Commands of the WeAct revision A protocol. */
enum Command : uint8_t
{
    CMD_WHO_AM_I = 0x81,  // Establish communication before driving the screen
    CMD_SET_ORIENTATION = 0x02,  // Sets the screen orientation
    CMD_SET_BRIGHTNESS = 0x03,  // Sets the screen brightness
    CMD_FULL = 0x04,  // Displays an image on the screen
    CMD_SET_BITMAP = 0x05,  // Displays an image on the screen
    CMD_SET_BITMAP_WITH_FASTLZ = 0x15,
    CMD_ENABLE_HUMITURE_REPORT = 0x06,
    CMD_FREE = 0x07,
    CMD_SYSTEM_VERSION = 0x42,
    CMD_END = 0x0A,  // Displays an image on the screen
    CMD_READ = 0x80,
};

}  // namespace weact_a

/** @brief Driver for the WeAct Studio displays, revision A. */
class LcdCommWeActA : public LcdComm
{
public:
    /** @brief Create the driver for the given COM port and screen size. */
    LcdCommWeActA(const std::string &com_port = "AUTO", int display_width = 320, int display_height = 480,
                  UpdateQueue *update_queue = nullptr);
    ~LcdCommWeActA() override;

    std::optional<std::string> auto_detect_com_port() override;

    /** @brief Send the command that sets the area of the next bitmap. */
    void Send_Bitmap_xy_Command(int xs, int ys, int xe, int ye, bool bypass_queue = false);
    /** @brief Send a command to the screen. */
    void SendCommand(const Bytes &byteBuffer, bool bypass_queue = false);

    /** @brief Initialize the screen. */
    void InitializeComm() override;
    /** @brief Reset the screen. */
    void Reset() override;
    /** @brief Clear the screen. */
    void Clear() override;
    /** @brief Fill the whole screen with a color. */
    void Full(RGBColor color = {0, 0, 0});
    /** @brief Turn the screen off. */
    void ScreenOff() override;
    /** @brief Turn the screen on. */
    void ScreenOn() override;
    /** @brief Set the brightness, from 0 to 100. */
    void SetBrightness(int level = 0) override;
    /** @brief Set the orientation of the screen. */
    void SetOrientation(Orientation orientation = Orientation::PORTRAIT) override;
    /** @brief Set the period of the sensor reports, in milliseconds. */
    bool SetSensorReportTime(int time_ms);
    /** @brief Release the resources of the driver. */
    void Free();
    /** @brief Read the temperature (°C) and humidity (%) reported by the screen sensor */
    std::pair<double, double> HandleSensorReport();
    /** @brief Display an image at (x, y), cropped to image_width x image_height if given. */
    void DisplayPILImage(const Image &image, int x = 0, int y = 0, int image_width = 0, int image_height = 0) override;

    /** @brief Last brightness set on the screen. */
    int brightness = 0;
    /** @brief Temperature read from the sensor, in degrees Celsius. */
    double temperature = 0;
    /** @brief Humidity read from the sensor, in percent. */
    double humidness = 0;
    /** @brief True if the screen supports FastLZ compressed images. */
    bool support_fastlz = false;
};
