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
 * @file lcd_simulated.h
 * @brief Simulated display, writing to a file (screencap.png) instead of a serial port. A web server displays the simulated screen with auto-refresh at http://localhost:5678
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "library/lcd/lcd_comm.h"

/** @brief File where the simulated screen is saved. */
constexpr const char *SCREENSHOT_FILE = "screencap.png";
/** @brief Port of the web server showing the simulated screen. */
constexpr int WEBSERVER_PORT = 5678;

/** @brief This webserver offer a blank page displaying simulated screen with auto-refresh */
class SimulatedLcdWebServer
{
public:
    /** @brief Throws std::runtime_error if the server cannot listen on the port */
    SimulatedLcdWebServer(const std::string &host, int port);
    ~SimulatedLcdWebServer();

    /** @brief Stop the web server. */
    void shutdown();

private:
    void serve_forever();
    void handle_client(intptr_t client_fd);

    intptr_t server_fd = -1;  ///< Listening socket (a SOCKET on Windows), -1 if closed
    std::atomic<bool> stopping = false;
    std::thread thread;
};

/** @brief Simulated display: write on a file instead of serial port */
class LcdSimulated : public LcdComm
{
public:
    /**
     * @brief Create a simulated display, the screen is saved to screenshot_file. The web server showing the screen (on
     * port 5678) is started only if webserver is true, and only one display can run it.
     */
    LcdSimulated(const std::string &com_port = "AUTO", int display_width = 320, int display_height = 480,
                 UpdateQueue *update_queue = nullptr, const std::string &screenshot_file = SCREENSHOT_FILE,
                 bool webserver = true);
    ~LcdSimulated() override;

    std::optional<std::string> auto_detect_com_port() override;
    /** @brief Close the simulated display, nothing to close. */
    void closeSerial() override;
    /** @brief Initialize the simulated display. */
    void InitializeComm() override;
    /** @brief Reset the simulated display. */
    void Reset() override;
    /** @brief Clear the simulated display. */
    void Clear() override;
    /** @brief Turn the simulated display off. */
    void ScreenOff() override;
    /** @brief Turn the simulated display on. */
    void ScreenOn() override;
    /** @brief Set the brightness of the simulated display, from 0 to 100. */
    void SetBrightness(int level = 25) override;
    /** @brief Set the color of the backplate LEDs, which the simulated display ignores. */
    void SetBackplateLedColor(RGBColor led_color = {255, 255, 255}) override;
    /** @brief Set the orientation of the simulated display. */
    void SetOrientation(Orientation orientation = Orientation::PORTRAIT) override;
    /** @brief Display an image at (x, y), cropped to image_width x image_height if given. */
    void DisplayPILImage(const Image &image, int x = 0, int y = 0, int image_width = 0, int image_height = 0) override;

    /** @brief Current content of the simulated screen */
    Image screen_image;

private:
    /** @brief Save the screen image to SCREENSHOT_FILE (through a temporary file, so that readers never get a partial file) */
    void save_screenshot();

    /** @brief File where the screen is saved */
    std::string screenshot_file_name;
    std::unique_ptr<SimulatedLcdWebServer> webServer;
};
