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

// C++ port of theme-editor.py: edit themes for System Monitor in a preview window on the computer.
// The preview window is refreshed as soon as the theme file is modified.

#include "theme-editor.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>

#include "library/config.h"
#include "library/display.h"
#include "library/lcd/lcd_simulated.h"
#include "library/log.h"
#include "library/stats.h"

namespace fs = std::filesystem;

namespace {

constexpr int RGB_LED_MARGIN = 12;

// Theme name given as argument
std::string theme_name;

LcdSimulated &simulated_lcd()
{
    return dynamic_cast<LcdSimulated &>(*display().lcd);
}

Glib::RefPtr<Gdk::Pixbuf> to_pixbuf(const Image &image)
{
    auto pixbuf = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, false, 8, image.width(), image.height());
    const int rowstride = pixbuf->get_rowstride();
    guint8 *pixels = pixbuf->get_pixels();
    for (int y = 0; y < image.height(); y++) {
        std::copy_n(image.data() + static_cast<size_t>(y) * image.width() * 3, image.width() * 3,
                    pixels + static_cast<size_t>(y) * rowstride);
    }
    return pixbuf;
}

std::string hex_color(RGBColor color)
{
    return std::format("#{:02x}{:02x}{:02x}", color.r, color.g, color.b);
}

}  // namespace

ThemeEditorWindow::ThemeEditorWindow()
{
    set_title("Turing SysMon Theme Editor");
    add_css_class("theme-editor");
    css = Gtk::CssProvider::create();
    Gtk::StyleContext::add_provider_for_display(get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Resize editor if display is too big (e.g. 8.8" displays are 1920x480), can be changed later by zoom buttons
    resize_factor = (display().lcd->get_width() > 1000 || display().lcd->get_height() > 1000) ? 2 : 1;

    // Get theme file to edit
    theme_file = fs::path(config::THEME_DATA["PATH"].as<std::string>()) / "theme.yaml";
    last_edit_time = fs::last_write_time(theme_file);
    logger.info("Using theme file {}", theme_file.string());

    // Open theme in default editor. You can also open the file manually in another program
    logger.info("Opening theme file in your default editor. If it does not work, open it manually in the editor of "
                "your choice");
    try {
        Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(theme_file.string()));
    } catch (const Glib::Error &e) {
        logger.warning("Cannot open the theme file in an editor: {}", e.what());
    }

    // Load theme file and generate first preview
    error_in_theme = !refresh_theme();

    set_child(box);
    viewer_picture.set_margin(RGB_LED_MARGIN);
    viewer_picture.set_halign(Gtk::Align::START);
    viewer_picture.set_cursor(Gdk::Cursor::create("crosshair"));
    viewer_picture.set_draw_func(sigc::mem_fun(*this, &ThemeEditorWindow::on_draw));
    box.append(viewer_picture);

    // Allow to click on preview to show coordinates and draw zones
    auto drag = Gtk::GestureDrag::create();
    drag->signal_drag_begin().connect(sigc::mem_fun(*this, &ThemeEditorWindow::on_drag_begin));
    drag->signal_drag_update().connect(sigc::mem_fun(*this, &ThemeEditorWindow::on_drag_update));
    drag->signal_drag_end().connect(sigc::mem_fun(*this, &ThemeEditorWindow::on_drag_end));
    viewer_picture.add_controller(drag);

    // Allow to resize editor using mouse wheel or buttons
    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect([this](double, double dy) {
        zoom(dy < 0 ? -0.2 : 0.2);
        return true;
    }, false);
    add_controller(scroll);

    zoom_plus_btn.signal_clicked().connect([this] { zoom(-0.2); });
    zoom_minus_btn.signal_clicked().connect([this] { zoom(0.2); });
    zoom_plus_btn.set_size_request(-1, 30);
    zoom_minus_btn.set_size_request(-1, 30);
    buttons.set_margin_start(RGB_LED_MARGIN);
    buttons.set_halign(Gtk::Align::START);
    buttons.append(zoom_plus_btn);
    buttons.append(zoom_minus_btn);
    box.append(buttons);

    label_coord.set_margin_top(10);
    box.append(label_coord);
    box.append(label_info);

    update_preview();
    apply_zoom();

    logger.info("You can now edit the theme file in the editor. When you save your changes, the preview window will "
                "update automatically");
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &ThemeEditorWindow::check_theme_file), 100);
}

int ThemeEditorWindow::display_width() const
{
    return static_cast<int>(display().lcd->get_width() / resize_factor);
}

int ThemeEditorWindow::display_height() const
{
    return static_cast<int>(display().lcd->get_height() / resize_factor);
}

bool ThemeEditorWindow::refresh_theme()
{
    try {
        config::load_theme(false);

        // Initialize the display
        display().initialize_display();

        // Create all static images
        display().display_static_images();

        // Create all static texts
        display().display_static_text();

        // Display all data on screen once
        const YAML::Node &theme = config::THEME_DATA;
        const YAML::Node stats_data = theme["STATS"];
        auto interval = [&](const YAML::Node &node) { return config::get(node, "INTERVAL", 0.0) > 0; };
        if (interval(stats_data["CPU"]["PERCENTAGE"])) stats::CPU::percentage();
        if (interval(stats_data["CPU"]["FREQUENCY"])) stats::CPU::frequency();
        if (interval(stats_data["CPU"]["LOAD"])) stats::CPU::load();
        if (interval(stats_data["CPU"]["TEMPERATURE"])) stats::CPU::temperature();
        if (interval(stats_data["CPU"]["FAN_SPEED"])) stats::CPU::fan_speed();
        if (interval(stats_data["GPU"])) stats::Gpu::stats();
        if (interval(stats_data["MEMORY"])) stats::Memory::stats();
        if (interval(stats_data["DISK"])) stats::Disk::stats();
        if (interval(stats_data["NET"])) stats::Net::stats();
        if (interval(stats_data["DATE"])) stats::Date::stats();
        if (interval(stats_data["UPTIME"])) stats::SystemUptime::stats();
        if (interval(stats_data["CUSTOM"])) stats::Custom::stats();
        if (interval(stats_data["WEATHER"])) stats::Weather::stats();
        if (interval(stats_data["PING"])) stats::Ping::stats();
        return true;
    } catch (const std::exception &e) {
        logger.error("Error in theme: {}", e.what());
        return false;
    }
}

void ThemeEditorWindow::update_preview()
{
    if (!error_in_theme) {
        preview = to_pixbuf(simulated_lcd().screen_image);
        const YAML::Node &theme = config::THEME_DATA;
        if (config::get(theme["display"], "DISPLAY_SIZE", "3.5\"") == "2.1\"") {
            // This is a circular screen: apply a circle mask over the preview
            try {
                const auto mask = Gdk::Pixbuf::create_from_file(
                    (config::MAIN_DIRECTORY / "res/backgrounds/circular-mask.png").string());
                mask->composite(preview, 0, 0, std::min(mask->get_width(), preview->get_width()),
                                std::min(mask->get_height(), preview->get_height()), 0, 0, 1, 1,
                                Gdk::InterpType::NEAREST, 255);
            } catch (const Glib::Error &e) {
                logger.warning("Cannot load circular mask: {}", e.what());
            }
        }
    } else {
        try {
            preview = Gdk::Pixbuf::create_from_file((config::MAIN_DIRECTORY / "res/docs/error-in-theme.png").string());
        } catch (const Glib::Error &) {
            // Error picture not found: red picture
            preview = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, false, 8, 100, 100);
            preview->fill(0xff0000ff);
        }
    }

    // Display RGB backplate LEDs color as background color, and the zone in the inverse color
    try {
        const YAML::Node &theme = config::THEME_DATA;
        led_color = config::get_color(theme["display"], "DISPLAY_RGB_LED", {255, 255, 255});
    } catch (const std::exception &) {
        led_color = {255, 255, 255};
    }
    // Text color readable on the background color
    const bool light = led_color.r * 299 + led_color.g * 587 + led_color.b * 114 > 128000;
    css->load_from_data("window.theme-editor { background-color: " + hex_color(led_color) +
                        "; } window.theme-editor > box > label { color: " + (light ? "#000000" : "#ffffff") + "; }");

    apply_zoom();
}

void ThemeEditorWindow::apply_zoom()
{
    const int width = display_width(), height = display_height();
    if (preview) {
        if (!error_in_theme) {
            scaled_preview = preview->scale_simple(std::max(1, width), std::max(1, height), Gdk::InterpType::BILINEAR);
        } else {
            const int size = std::max(1, std::min(width, height));
            scaled_preview = preview->scale_simple(size, size, Gdk::InterpType::BILINEAR);
        }
    }
    viewer_picture.set_content_width(width);
    viewer_picture.set_content_height(height);
    zoom_plus_btn.set_size_request(width / 2, 30);
    zoom_minus_btn.set_size_request(width / 2, 30);
    set_default_size(width + 2 * RGB_LED_MARGIN, height + 2 * RGB_LED_MARGIN + 80);
    viewer_picture.queue_draw();
}

void ThemeEditorWindow::zoom(double delta)
{
    const double previous = resize_factor;
    resize_factor = std::max(0.2, resize_factor + delta);
    if (resize_factor != previous) {
        logger.info("Zoom level changed from {:.1f} to {:.1f}", previous, resize_factor);
        zone.reset();
        apply_zoom();
    }
}

bool ThemeEditorWindow::check_theme_file()
{
    std::error_code ec;
    const auto edit_time = fs::last_write_time(theme_file, ec);
    if (!ec && edit_time > last_edit_time) {
        logger.info("The theme file has been updated, the preview window will refresh");
        error_in_theme = !refresh_theme();
        last_edit_time = edit_time;

        // Update the preview.png that is in the theme folder
        try {
            simulated_lcd().screen_image.save(config::THEME_DATA["PATH"].as<std::string>() + "preview.png");
        } catch (const std::exception &e) {
            logger.error("Cannot save preview.png: {}", e.what());
        }

        // Display new picture
        update_preview();
    }
    return true;  // keep the timer
}

void ThemeEditorWindow::on_draw(const Cairo::RefPtr<Cairo::Context> &cr, int, int)
{
    if (scaled_preview) {
        Gdk::Cairo::set_source_pixbuf(cr, scaled_preview, 0, 0);
        cr->paint();
    }
    if (zone) {
        cr->set_source_rgb((255 - led_color.r) / 255.0, (255 - led_color.g) / 255.0, (255 - led_color.b) / 255.0);
        cr->rectangle(zone->x, zone->y, zone->width, zone->height);
        cr->fill();
    }
}

void ThemeEditorWindow::on_drag_begin(double x, double y)
{
    x0 = static_cast<int>(x);
    y0 = static_cast<int>(y);
    zone.reset();
    viewer_picture.queue_draw();
}

void ThemeEditorWindow::on_drag_update(double offset_x, double offset_y)
{
    // Do not draw zone outside of theme preview
    const int x1 = std::clamp(static_cast<int>(x0 + offset_x), 0, display_width() - 1);
    const int y1 = std::clamp(static_cast<int>(y0 + offset_y), 0, display_height() - 1);

    label_coord.set_text(std::format("Drawing zone from [{:.0f},{:.0f}] to [{:.0f},{:.0f}]", x0 * resize_factor,
                                     y0 * resize_factor, x1 * resize_factor, y1 * resize_factor));
    const int width = std::abs(x1 - x0), height = std::abs(y1 - y0);
    if (width > 0 && height > 0) {
        zone = Zone{std::min(x0, x1), std::min(y0, y1), width, height};
    } else {
        zone.reset();
    }
    viewer_picture.queue_draw();
}

void ThemeEditorWindow::on_drag_end(double offset_x, double offset_y)
{
    if (static_cast<int>(offset_x) != 0 || static_cast<int>(offset_y) != 0) {
        // Do not draw zone outside of theme preview
        const int x1 = std::clamp(static_cast<int>(x0 + offset_x), 0, display_width() - 1);
        const int y1 = std::clamp(static_cast<int>(y0 + offset_y), 0, display_height() - 1);

        // Display drawn zone and coordinates
        const int x = std::min(x0, x1), y = std::min(y0, y1);
        const int width = std::abs(x1 - x0), height = std::abs(y1 - y0);
        if (width > 0 && height > 0) {
            zone = Zone{x, y, width, height};
        } else {
            zone.reset();
        }

        // Display relative zone coordinates, to set in theme
        label_coord.set_text(std::format("Zone: X={:.0f}, Y={:.0f}, width={:.0f} height={:.0f}", x * resize_factor,
                                         y * resize_factor, width * resize_factor, height * resize_factor));
    } else {
        // Display click coordinates
        label_coord.set_text(std::format("X={:.0f}, Y={:.0f} (click and drag to draw a zone)", x0 * resize_factor,
                                         y0 * resize_factor));
    }
    viewer_picture.queue_draw();
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::printf("Usage :\n");
        std::printf("        theme-editor theme-name\n");
        std::printf("Examples : \n");
        std::printf("        theme-editor 3.5inchTheme2\n");
        std::printf("        theme-editor Landscape6Grid\n");
        std::printf("        theme-editor Cyberpunk\n");
        return 0;
    }
    theme_name = argv[1];

    // Only display the information and errors of the library in the editor
    logger.setLevel(LogLevel::INFO);

    // Hardcode specific configuration for theme editor
    config::init(false);
    config::CONFIG_DATA["config"]["HW_SENSORS"] = "STATIC";  // For theme editor always use stub data
    config::CONFIG_DATA["config"]["THEME"] = theme_name;  // Theme is given as argument
    config::load_theme();

    // For theme editor, always use simulated LCD
    config::CONFIG_DATA["display"]["REVISION"] = "SIMU";

    stats::init();

    logger.info("Starting Theme Editor...");
    auto app = Gtk::Application::create("io.github.turing_smart_screen.ThemeEditor");
    // The theme name argument is not given to GTK
    char *gtk_argv[] = {argv[0], nullptr};
    const int result = app->make_window_and_run<ThemeEditorWindow>(1, gtk_argv);
    logger.info("Exit Theme Editor...");
    // Exit without waiting for the simulated display web server
    std::_Exit(result);
}
