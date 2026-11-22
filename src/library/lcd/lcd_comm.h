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
 * @file lcd_comm.h
 * @brief Base class of all LCD screen drivers. It renders text, bitmaps, progress bars, line graphs and radial progress bars into images, then the driver (subclass) sends the images to the screen with DisplayPILImage().
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "library/lcd/color.h"
#include "library/lcd/font.h"
#include "library/lcd/image.h"
#include "library/lcd/serial_port.h"

/** @brief Orientation of the screen: the image is rotated accordingly before being sent. */
enum class Orientation : int
{
    PORTRAIT = 0,
    LANDSCAPE = 2,
    REVERSE_PORTRAIT = 1,
    REVERSE_LANDSCAPE = 3,
};

/**
 * @brief The screen resets itself when the program starts, so its COM port can disappear or change for a
 * few seconds: opening it is retried instead of giving up (and exiting) on the first failure.
 */
constexpr int SERIAL_OPEN_ATTEMPTS = 10;
constexpr int SERIAL_OPEN_RETRY_DELAY = 1;  ///< seconds

/** @brief Thread-safe FIFO of requests to send to the screen (queue of (function, args) tuples) */
class UpdateQueue
{
public:
    /** @brief Request to execute on the update queue thread. */
    using Request = std::function<void()>;

    /** @brief Add a request to the queue. */
    void put(Request request);

    /** @brief Wait for a request and remove it from the queue */
    Request get();

    /** @brief Same as get(), but returns std::nullopt if no request arrives within the timeout */
    std::optional<Request> get(std::chrono::milliseconds timeout);

    /** @brief True if the queue has no pending request. */
    bool empty() const;

private:
    mutable std::mutex mutex;
    std::condition_variable not_empty;
    std::deque<Request> requests;
};

/** @brief Parameters of LcdComm::DisplayText() */
struct TextOptions
{
    /** @brief Text to display. */
    std::string text = "";
    /** @brief X coordinate of the text box, in pixels. */
    int x = 0;
    /** @brief Y coordinate of the text box, in pixels. */
    int y = 0;
    /** @brief Width of the text box, in pixels. */
    int width = 0;
    /** @brief Height of the text box, in pixels. */
    int height = 0;
    /** @brief Path of the TrueType font file. */
    std::string font = "./res/fonts/roboto-mono/RobotoMono-Regular.ttf";
    /** @brief Font size, in pixels. */
    int font_size = 20;
    /** @brief Color of the text. */
    RGBColor font_color = {0, 0, 0};
    /** @brief Background color of the text box. */
    RGBColor background_color = {255, 255, 255};
    /** @brief Provide the background image path to display text with transparent background */
    std::optional<std::string> background_image = std::nullopt;
    /** @brief Horizontal alignment of the text: left, center or right. */
    std::string align = "left";
    /** @brief Text anchor (Pillow syntax, 2 characters). */
    std::string anchor = "la";
};

/** @brief Parameters of LcdComm::DisplayProgressBar() */
struct ProgressBarOptions
{
    /** @brief X coordinate of the progress bar, in pixels. */
    int x = 0;
    /** @brief Y coordinate of the progress bar, in pixels. */
    int y = 0;
    /** @brief Width of the progress bar, in pixels. */
    int width = 0;
    /** @brief Height of the progress bar, in pixels. */
    int height = 0;
    /** @brief Value at the start of the bar. */
    double min_value = 0;
    /** @brief Value at the end of the bar. */
    double max_value = 100;
    /** @brief Current value, displayed as the filled part of the bar. */
    double value = 50;
    /** @brief Color of the filled part of the bar. */
    RGBColor bar_color = {0, 0, 0};
    /** @brief Draw an outline around the bar. */
    bool bar_outline = true;
    /** @brief Background color of the bar. */
    RGBColor background_color = {255, 255, 255};
    /** @brief Provide the background image path to display progress bar with transparent background */
    std::optional<std::string> background_image = std::nullopt;
    /** @brief Fill the bar from the end instead of the start. */
    bool reverse_direction = false;
};

/** @brief Parameters of LcdComm::DisplayLineGraph() */
struct LineGraphOptions
{
    /** @brief X coordinate of the line graph, in pixels. */
    int x = 0;
    /** @brief Y coordinate of the line graph, in pixels. */
    int y = 0;
    /** @brief Width of the line graph, in pixels. */
    int width = 0;
    /** @brief Height of the line graph, in pixels. */
    int height = 0;
    std::vector<double> values = {};  ///< NaN values are not plotted
    /** @brief Lowest value of the graph, unless autoscale is enabled. */
    double min_value = 0;
    /** @brief Highest value of the graph, unless autoscale is enabled. */
    double max_value = 100;
    /** @brief Scale the graph to the minimum and maximum of the values. */
    bool autoscale = false;
    /** @brief Color of the line. */
    RGBColor line_color = {0, 0, 0};
    /** @brief Width of the line, in pixels. */
    int line_width = 2;
    /** @brief Draw the axes of the graph. */
    bool graph_axis = true;
    /** @brief Color of the axes. */
    RGBColor axis_color = {0, 0, 0};
    /** @brief Path of the font file used for the axis labels. */
    std::string axis_font = "./res/fonts/roboto/Roboto-Black.ttf";
    /** @brief Font size of the axis labels, in pixels. */
    int axis_font_size = 10;
    /** @brief Background color of the graph. */
    RGBColor background_color = {255, 255, 255};
    /** @brief Provide the background image path to display plot graph with transparent background */
    std::optional<std::string> background_image = std::nullopt;
    std::string axis_minmax_format = "{:0.0f}";  ///< std::format syntax
};

/** @brief Parameters of LcdComm::DisplayRadialProgressBar() */
struct RadialProgressBarOptions
{
    /** @brief X coordinate of the center of the radial bar, in pixels. */
    int xc = 0;
    /** @brief Y coordinate of the center of the radial bar, in pixels. */
    int yc = 0;
    /** @brief Radius of the bar, in pixels. */
    int radius = 0;
    /** @brief Thickness of the bar, in pixels. */
    int bar_width = 0;
    /** @brief Value at the start of the bar. */
    double min_value = 0;
    /** @brief Value at the end of the bar. */
    double max_value = 100;
    /** @brief Start angle of the bar, in degrees (0 is at 3 o'clock). */
    double angle_start = 0;
    /** @brief End angle of the bar, in degrees. */
    double angle_end = 360;
    /** @brief Angle between two decoration marks, in degrees. */
    int angle_sep = 5;
    /** @brief Number of decoration steps. */
    int angle_steps = 10;
    /** @brief Fill the bar clockwise. */
    bool clockwise = true;
    /** @brief Current value. */
    double value = 50;
    /** @brief Text displayed in the center. If empty, the percentage is displayed */
    std::optional<std::string> text = std::nullopt;
    /** @brief Display the value in the center of the bar. */
    bool with_text = true;
    /** @brief Path of the font file used for the value text. */
    std::string font = "./res/fonts/roboto/Roboto-Black.ttf";
    /** @brief Font size of the value text, in pixels. */
    int font_size = 20;
    /** @brief Color of the value text. */
    RGBColor font_color = {0, 0, 0};
    /** @brief Color of the filled part of the bar. */
    RGBColor bar_color = {0, 0, 0};
    /** @brief Background color of the bar. */
    RGBColor background_color = {255, 255, 255};
    /** @brief Provide the background image path to display progress bar with transparent background */
    std::optional<std::string> background_image = std::nullopt;
    /** @brief Custom bounding box of the text (left, top, right, bottom), in pixels. */
    std::tuple<int, int, int, int> custom_bbox = {0, 0, 0, 0};
    /** @brief Offset of the text from the center of the bar, in pixels. */
    std::pair<int, int> text_offset = {0, 0};
    /** @brief Color of the bar background. */
    RGBColor bar_background_color = {0, 0, 0};
    /** @brief Draw the background of the bar. */
    bool draw_bar_background = false;
    std::string bar_decoration = "";  ///< "" or "Ellipse"
};

/** @brief Base class of all display drivers: serial port access, text and graphics rendering, update queue. */
class LcdComm
{
public:
    /** @brief Create a driver for the given COM port and screen size. */
    LcdComm(std::string com_port = "AUTO", int display_width = 320, int display_height = 480,
            UpdateQueue *update_queue = nullptr);
    virtual ~LcdComm();

    LcdComm(const LcdComm &) = delete;
    LcdComm &operator=(const LcdComm &) = delete;

    /** @brief Width of the screen, in pixels, for the current orientation. */
    int get_width() const;
    /** @brief Height of the screen, in pixels, for the current orientation. */
    int get_height() const;

    /** @brief Open the serial port. */
    void openSerial();
    /** @brief Close the serial port. */
    virtual void closeSerial();

    /** @brief Write data to the serial port. */
    void serial_write(const std::vector<uint8_t> &data);
    /** @brief Read size bytes from the serial port. */
    std::vector<uint8_t> serial_read(size_t size);
    /** @brief Read all the bytes available on the serial port. */
    std::vector<uint8_t> serial_readall();
    /** @brief Discard the data waiting in the serial input buffer. */
    void serial_flush_input();

    /** @brief Write data to the screen. */
    void WriteData(const std::vector<uint8_t> &byteBuffer);
    /** @brief Send a line of data to the screen, through the update queue if there is one. */
    void SendLine(const std::vector<uint8_t> &line);
    /** @brief Write a line of data to the screen. */
    void WriteLine(const std::vector<uint8_t> &line);
    /** @brief Read readSize bytes from the screen. */
    std::vector<uint8_t> ReadData(size_t readSize);

    /** @brief To implement only for screens that use serial commands */
    virtual std::optional<std::string> auto_detect_com_port();

    /** @brief Initialize the screen. */
    virtual void InitializeComm() = 0;
    /** @brief Reset the screen. */
    virtual void Reset() = 0;
    /** @brief Clear the screen. */
    virtual void Clear() = 0;
    /** @brief Turn the screen off. */
    virtual void ScreenOff() = 0;
    /** @brief Turn the screen on. */
    virtual void ScreenOn() = 0;
    /** @brief Set the brightness of the screen, from 0 (off) to 100 (maximum). */
    virtual void SetBrightness(int level) = 0;
    /** @brief Set the color of the backplate LEDs, on the models that have them. */
    virtual void SetBackplateLedColor(RGBColor led_color = {255, 255, 255});
    /** @brief Set the orientation of the screen. */
    virtual void SetOrientation(Orientation orientation) = 0;
    /** @brief Display an image at (x, y), cropped to image_width x image_height if given. */
    virtual void DisplayPILImage(const Image &image, int x = 0, int y = 0, int image_width = 0,
                                 int image_height = 0) = 0;

    /** @brief Display a bitmap file at (x, y), cropped to width x height if given. */
    void DisplayBitmap(const std::string &bitmap_path, int x = 0, int y = 0, int width = 0, int height = 0);

    /** @brief Convert text to bitmap and display it */
    void DisplayText(const TextOptions &options);

    /** @brief Generate a progress bar and display it */
    void DisplayProgressBar(const ProgressBarOptions &options);

    /** @brief Generate a plot graph and display it */
    void DisplayLineGraph(const LineGraphOptions &options);

    /** @brief Draw the decoration (tick mark) of a radial bar at the given angle. */
    void DrawRadialDecoration(ImageDraw &draw, double angle, double radius, double width, RGBColor color = {0, 0, 0});

    /** @brief Generate a radial progress bar and display it */
    void DisplayRadialProgressBar(const RadialProgressBarOptions &options);

    /** @brief Load image from the filesystem, or get from the cache if it has already been loaded previously */
    Image open_image(const std::string &bitmap_path);

    /** @brief Open a font, the same font is shared between the calls with the same name and size. */
    std::shared_ptr<Font> open_font(const std::string &name, int size);

    /** @brief If set, openSerial() uses this function to create the serial port instead of opening a real one (tests) */
    static inline std::function<std::unique_ptr<SerialPort>()> serial_factory;

    /** @brief Serial port connected to the screen. */
    std::unique_ptr<SerialPort> lcd_serial;

    /**
     * @brief String containing absolute path to serial port e.g. "/dev/ttyACM1" or "AUTO" for auto-discovery
     * Ignored for USB HID screens
     */
    std::string com_port;

    /** @brief Display always start in portrait orientation by default */
    Orientation orientation = Orientation::PORTRAIT;
    /** @brief Display width in default orientation (portrait) */
    int display_width;
    /** @brief Display height in default orientation (portrait) */
    int display_height;

    /**
     * @brief Queue containing the serial requests to send to the screen. An external thread should run to process requests
     * on the queue. If you want serial requests to be done in sequence, set it to nullptr
     */
    UpdateQueue *update_queue;

    /**
     * @brief Mutex to protect the queue in case a thread want to add multiple requests (e.g. image data) that should not be
     * mixed with other requests in-between
     */
    std::mutex update_queue_mutex;

private:
    /** @brief Protects the caches below, used from several threads */
    std::mutex cache_mutex;

    /** @brief Cache to store opened images, to avoid opening and loading from the filesystem every time */
    std::map<std::string, Image> image_cache;

    /** @brief Cache to store opened fonts, to avoid opening and loading from the filesystem every time */
    std::map<std::pair<std::string, int>, std::shared_ptr<Font>> font_cache;

    /**
     * @brief Remember the bounding box drawn by the last DisplayText() call at a given (x, y) origin, so the
     * next call can redraw the union of the old and new areas and fully erase any previous text that is
     * narrower/shorter than what replaces it, regardless of font metrics ("ghosting" prevention).
     */
    std::map<std::pair<int, int>, std::tuple<int, int, int, int>> text_bbox_cache;
};
