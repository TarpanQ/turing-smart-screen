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
 * @file simple-program.cpp
 * @brief A simple test program using the library code to display custom content on screen. It does not use the configuration file and themes: change the constants below for your screen.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <exception>
#include <format>
#include <memory>
#include <string>

#include "library/config.h"
#include "library/lcd/lcd_comm_rev_a.h"
#include "library/lcd/lcd_comm_rev_b.h"
#include "library/lcd/lcd_comm_rev_c.h"
#include "library/lcd/lcd_comm_rev_d.h"
#include "library/lcd/lcd_comm_turing_usb.h"
#include "library/lcd/lcd_comm_weact_a.h"
#include "library/lcd/lcd_comm_weact_b.h"
#include "library/lcd/lcd_simulated.h"
#include "library/log.h"

/** @brief Set your COM port e.g. "/dev/ttyACM0", or "AUTO" for auto-discovery */
const std::string COM_PORT = "AUTO";

/**
 * @brief Display revision:
 * - A      for Turing 3.5" and UsbPCMonitor 3.5"/5"
 * - B      for Xuanfang 3.5" (inc. flagship)
 * - C      for Turing 5"
 * - D      for Kipye Qiye Smart Display 3.5"
 * - TUR_USB for Turing USB models (4.6"/5.2"/8"/8.8" HW rev 1.x/9.2")
 * - WEACT_A for WeAct Studio Display FS V1 3.5"
 * - WEACT_B for WeAct Studio Display FS V1 0.96"
 * - SIMU   for simulated display (image written in screencap.png)
 * To identify your smart screen: https://github.com/mathoudebine/turing-smart-screen-python/wiki/Hardware-revisions
 */
const std::string REVISION = "A";

/**
 * @brief Display width & height in pixels for portrait orientation
 * /!\ Do not switch width/height here for landscape, use lcd_comm.SetOrientation below
 * 320x480 for 3.5" models
 * 480x480 for 2.1" models
 * 480x800 for 4.3" and 5" models
 * 480x1920 for 8.8" models
 */
/** @brief Width of the screen, in pixels. */
constexpr int WIDTH = 320;
/** @brief Height of the screen, in pixels. */
constexpr int HEIGHT = 480;

static_assert(WIDTH <= HEIGHT, "Indicate display width/height for PORTRAIT orientation: width <= height");

namespace {

std::atomic<bool> stop = false;

void sighandler(int)
{
    stop = true;
}

std::string time_now()
{
    // Same format as Python str(datetime.now().time()): HH:MM:SS.ffffff
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    return std::format("{:02}:{:02}:{:02}.{:06}", tm.tm_hour, tm.tm_min, tm.tm_sec, micros);
}

double elapsed(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

/** @brief Entry point of the example program. */
int main()
{
    // Set the signal handlers, to send a complete frame to the LCD before exit
    std::signal(SIGINT, sighandler);
    std::signal(SIGTERM, sighandler);
    std::signal(SIGQUIT, sighandler);

    // Resources (fonts, backgrounds) are in the res/ directory of the project
    const std::string res = (config::find_main_directory() / "res").string() + "/";

    try {
        // Build your LcdComm object based on the HW revision
        std::unique_ptr<LcdComm> lcd_comm;
        if (REVISION == "A") {
            logger.info("Selected Hardware Revision A (Turing Smart Screen 3.5\" & UsbPCMonitor 3.5\"/5\")");
            lcd_comm = std::make_unique<LcdCommRevA>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "B") {
            logger.info("Selected Hardware Revision B (XuanFang screen 3.5\" version B / flagship)");
            lcd_comm = std::make_unique<LcdCommRevB>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "C") {
            logger.info("Selected Hardware Revision C (Turing Smart Screen 5\")");
            lcd_comm = std::make_unique<LcdCommRevC>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "D") {
            logger.info("Selected Hardware Revision D (Kipye Qiye Smart Display 3.5\")");
            lcd_comm = std::make_unique<LcdCommRevD>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "TUR_USB") {
            logger.info("Selected Hardware Revision Turing USB (newer models 4.6\"/5.2\"/8\"/8.8\" HW rev 1.x/9.2\")");
            lcd_comm = std::make_unique<LcdCommTuringUSB>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "WEACT_A") {
            logger.info("Selected Hardware WeAct Studio Display FS V1 3.5\"");
            lcd_comm = std::make_unique<LcdCommWeActA>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "WEACT_B") {
            logger.info("Selected Hardware WeAct Studio Display FS V1 0.96\"");
            lcd_comm = std::make_unique<LcdCommWeActB>(COM_PORT, WIDTH, HEIGHT);
        } else if (REVISION == "SIMU") {
            logger.info("Selected Simulated LCD");
            lcd_comm = std::make_unique<LcdSimulated>("AUTO", WIDTH, HEIGHT);
        } else {
            logger.error("Unknown revision");
            return 1;
        }

        // Reset screen in case it was in an unstable state (screen is also cleared)
        lcd_comm->Reset();

        // Send initialization commands
        lcd_comm->InitializeComm();

        // Set brightness in % (warning: revision A display can get hot at high brightness! Keep value at 50% max)
        lcd_comm->SetBrightness(10);

        // Set backplate RGB LED color (for supported HW only)
        lcd_comm->SetBackplateLedColor({255, 255, 255});

        // Set orientation (screen starts in Portrait)
        lcd_comm->SetOrientation(Orientation::LANDSCAPE);

        // Define background picture
        const std::string background =
            std::format("{}backgrounds/example_{}x{}.png", res, lcd_comm->get_width(), lcd_comm->get_height());

        // Display sample picture
        logger.debug("setting background picture");
        auto start = std::chrono::steady_clock::now();
        lcd_comm->DisplayBitmap(background);
        logger.debug("background picture set (took {:.3f} s)", elapsed(start));

        // Display sample text
        lcd_comm->DisplayText({.text = "Basic text", .x = 50, .y = 85, .font = res + "fonts/roboto-mono/RobotoMono-Regular.ttf"});

        // Display custom text with solid background
        lcd_comm->DisplayText({.text = "Custom italic multiline text\nright-aligned",
                               .x = 5,
                               .y = 120,
                               .font = res + "fonts/roboto/Roboto-Italic.ttf",
                               .font_size = 20,
                               .font_color = {0, 0, 255},
                               .background_color = {255, 255, 0},
                               .align = "right"});

        // Display custom text with transparent background
        lcd_comm->DisplayText({.text = "Transparent bold text",
                               .x = 5,
                               .y = 180,
                               .font = res + "fonts/geforce/GeForce-Bold.ttf",
                               .font_size = 30,
                               .font_color = {255, 255, 255},
                               .background_image = background});

        // Display the current time and some progress bars as fast as possible
        int bar_value = 0;
        while (!stop) {
            start = std::chrono::steady_clock::now();
            lcd_comm->DisplayText({.text = time_now(),
                                   .x = 160,
                                   .y = 2,
                                   .font = res + "fonts/roboto/Roboto-Bold.ttf",
                                   .font_size = 20,
                                   .font_color = {255, 0, 0},
                                   .background_image = background});

            lcd_comm->DisplayProgressBar({.x = 10, .y = 40, .width = 140, .height = 30, .min_value = 0,
                                          .max_value = 100, .value = static_cast<double>(bar_value),
                                          .bar_color = {255, 255, 0}, .bar_outline = true,
                                          .background_image = background});

            lcd_comm->DisplayProgressBar({.x = 160, .y = 40, .width = 140, .height = 30, .min_value = 0,
                                          .max_value = 19, .value = static_cast<double>(bar_value % 20),
                                          .bar_color = {0, 255, 0}, .bar_outline = false,
                                          .background_image = background});

            lcd_comm->DisplayRadialProgressBar({.xc = 98, .yc = 260, .radius = 25, .bar_width = 4,
                                                .min_value = 0, .max_value = 100, .angle_sep = 0,
                                                .value = static_cast<double>(bar_value),
                                                .font = res + "fonts/roboto/Roboto-Black.ttf",
                                                .font_color = {255, 255, 255}, .bar_color = {0, 255, 0},
                                                .background_image = background});

            lcd_comm->DisplayRadialProgressBar({.xc = 222, .yc = 260, .radius = 40, .bar_width = 13,
                                                .min_value = 0, .max_value = 100, .angle_start = 405,
                                                .angle_end = 135, .angle_sep = 5, .angle_steps = 10,
                                                .clockwise = false, .value = static_cast<double>(bar_value),
                                                .text = std::format("{}°C", 10 * (bar_value / 10)),
                                                .font = res + "fonts/geforce/GeForce-Bold.ttf", .font_size = 20,
                                                .font_color = {255, 255, 0}, .bar_color = {255, 255, 0},
                                                .background_image = background});

            bar_value = (bar_value + 2) % 101;
            logger.debug("refresh done (took {:.3f} s)", elapsed(start));
        }

        // Close serial connection at exit
        lcd_comm->closeSerial();
    } catch (const std::exception &e) {
        logger.error("{}", e.what());
        return 1;
    }
    return 0;
}
