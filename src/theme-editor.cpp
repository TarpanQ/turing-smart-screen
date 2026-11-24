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
 * @file theme-editor.cpp
 * @brief Edit themes for System Monitor in a preview window on the computer. The preview window is refreshed as soon as the theme file is modified.
 */

#include "theme-editor.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "library/config.h"
#include "library/display.h"
#include "library/lcd/lcd_simulated.h"
#include "library/log.h"
#include "library/scheduler.h"
#include "library/sensors/sensors_custom.h"
#include "library/stats.h"

namespace fs = std::filesystem;

namespace {

constexpr int RGB_LED_MARGIN = 12;
/** @brief Width of the text editor, on the left of the preview */
constexpr int TEXT_EDITOR_WIDTH = 420;
/** @brief Width of the list of display elements, between the text editor and the preview */
constexpr int ITEM_LIST_WIDTH = 300;
/** @brief Delay (ms) without typing before the text is saved */
constexpr unsigned int SAVE_DELAY_MS = 500;
/** @brief Delay (ms) between two colors of the zone of the selected element */
constexpr unsigned int BLINK_PERIOD_MS = 500;

/** @brief Theme name given as argument */
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

/** @brief Parsed theme text, std::nullopt if it is not valid YAML */
std::optional<YAML::Node> parse_yaml(const Glib::ustring &text)
{
    try {
        return YAML::Load(std::string(text));
    } catch (const YAML::Exception &) {
        return std::nullopt;
    }
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

    // Load theme file and generate first preview. The text areas are recorded to draw the zone of the selected element
    display().lcd->set_record_text_areas(true);
    error_in_theme = !refresh_theme();

    // Text of the theme on the left, preview and buttons on the right
    text_scroll.set_child(text_view);
    text_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    text_scroll.set_size_request(TEXT_EDITOR_WIDTH, -1);
    text_scroll.set_vexpand(true);
    text_view.set_monospace(true);
    text_view.set_wrap_mode(Gtk::WrapMode::NONE);
    load_text();
    text_changed = text_view.get_buffer()->signal_changed().connect([this] { on_text_changed(); });

    // List of the display elements between the text and the preview
    build_item_list();
    update_item_states();
    items_scroll.set_child(item_list);
    items_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    items_scroll.set_size_request(ITEM_LIST_WIDTH, -1);
    items_scroll.set_vexpand(true);

    main_box.append(text_scroll);
    main_box.append(items_scroll);
    main_box.append(box);
    set_child(main_box);

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

    // Test Hotscreen: active if a display of config.json uses this theme
    const auto configured = config::load_displays(config::CONFIG_DATA);
    for (size_t index = 0; index < configured.size(); index++) {
        if (configured[index].theme == theme_name) {
            test_settings = configured[index];
            test_index = index;
            break;
        }
    }
    test_button.set_sensitive(test_settings.has_value());
    test_button.set_halign(Gtk::Align::START);
    test_button.set_margin_start(RGB_LED_MARGIN);
    test_button.signal_clicked().connect([this] { on_test_clicked(); });
    test_done.connect([this] {
        if (test_thread.joinable()) {
            test_thread.join();
        }
        test_button.set_sensitive(test_settings.has_value());
        label_coord.set_text(test_result);
    });
    box.append(test_button);

    label_coord.set_margin_top(10);
    box.append(label_coord);
    box.append(label_info);

    update_preview();
    apply_zoom();

    label_info.set_text("The theme is saved automatically while you type, and the preview is refreshed after each save");
    logger.info("You can now edit the theme in the text on the left: changes are saved automatically and the preview "
                "is refreshed");
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &ThemeEditorWindow::check_theme_file), 100);
    // Blinking zone of the selected element: the color changes every BLINK_PERIOD_MS
    blink_timer = Glib::signal_timeout().connect(
        [this] {
            blink_on = !blink_on;
            viewer_picture.queue_draw();
            return true;
        },
        BLINK_PERIOD_MS);
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
        display().load_theme();
        // The areas of the texts are recorded again by this rendering
        display().lcd->clear_text_areas();

        // Initialize the display
        display().initialize_display();

        // Create all static images
        display().display_static_images();

        // Create all static texts
        display().display_static_text();

        // Display all data on screen once, for the stats that the program updates (same intervals as the program)
        const YAML::Node &theme = config::THEME_DATA;
        auto enabled = [&](const std::vector<std::string> &measure) {
            std::vector<std::string> path = {"STATS"};
            path.insert(path.end(), measure.begin(), measure.end());
            return stats::item_interval(theme, path) > 0;
        };
        if (enabled({"CPU", "PERCENTAGE"})) stats::CPU::percentage();
        if (enabled({"CPU", "FREQUENCY"})) stats::CPU::frequency();
        if (enabled({"CPU", "LOAD"})) stats::CPU::load();
        if (enabled({"CPU", "TEMPERATURE"})) stats::CPU::temperature();
        if (enabled({"CPU", "FAN_SPEED"})) stats::CPU::fan_speed();
        if (enabled({"CPU", "MODEL"})) stats::CPU::model();
        if (enabled({"GPU"})) stats::Gpu::stats();
        if (enabled({"GPU", "MODEL"})) stats::Gpu::model();
        if (enabled({"MEMORY"})) stats::Memory::stats();
        if (enabled({"MEMORY", "MODEL"})) stats::Memory::model();
        if (enabled({"DISK"})) stats::Disk::stats();
        if (enabled({"NET"})) stats::Net::stats();
        if (enabled({"DATE"})) stats::Date::stats();
        if (enabled({"UPTIME"})) stats::SystemUptime::stats();
        if (enabled({"CUSTOM"})) stats::Custom::stats();
        if (enabled({"WEATHER"})) stats::Weather::stats();
        if (enabled({"PING"})) stats::Ping::stats();
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
    // Same height as the zoom buttons, and same width as both of them
    test_button.set_size_request(2 * (width / 2), 30);
    set_default_size(width + 2 * RGB_LED_MARGIN + TEXT_EDITOR_WIDTH + ITEM_LIST_WIDTH, height + 2 * RGB_LED_MARGIN + 80);
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
    // Only changes made outside the editor are checked here: the editor refreshes the preview after its own saves
    std::error_code ec;
    const auto edit_time = fs::last_write_time(theme_file, ec);
    if (ec || edit_time <= last_edit_time || asking_reload) {
        return true;  // keep the timer
    }
    if (text_dirty) {
        // Typed changes are not saved yet: the user chooses which version is kept
        ask_reload(edit_time);
    } else {
        reload_from_file(edit_time);
    }
    return true;  // keep the timer
}

void ThemeEditorWindow::reload_from_file(fs::file_time_type edit_time)
{
    logger.info("The theme file has been updated, the preview window will refresh");
    load_text();
    update_item_states();
    last_edit_time = edit_time;
    refresh_preview();
}

void ThemeEditorWindow::ask_reload(fs::file_time_type edit_time)
{
    asking_reload = true;
    reload_dialog = Gtk::AlertDialog::create("The theme file was changed outside the editor");
    reload_dialog->set_detail("The editor has changes that are not saved. Keep your changes (they replace the file), or "
                              "load the file from disk (your changes are lost).");
    reload_dialog->set_buttons({"Keep my changes", "Load the file"});
    reload_dialog->set_default_button(0);
    reload_dialog->set_cancel_button(0);  // closing the dialog keeps the changes of the editor
    reload_dialog->choose(*this, [this, edit_time](const Glib::RefPtr<Gio::AsyncResult> &result) {
        asking_reload = false;
        int button = 0;
        try {
            button = reload_dialog->choose_finish(result);
        } catch (const Glib::Error &e) {
            logger.error("Cannot show the reload dialog: {}", e.what());
        }
        if (button == 1) {
            text_dirty = false;
            reload_from_file(edit_time);
        } else {
            // Keep the changes of the editor: saving them replaces the file and refreshes the preview
            last_edit_time = edit_time;
            save_text();
        }
    });
}

void ThemeEditorWindow::refresh_preview()
{
    error_in_theme = !refresh_theme();
    update_selected_zone();

    // Update the preview.png that is in the theme folder
    try {
        simulated_lcd().screen_image.save(config::THEME_DATA["PATH"].as<std::string>() + "preview.png");
    } catch (const std::exception &e) {
        logger.error("Cannot save preview.png: {}", e.what());
    }

    // Display new picture
    update_preview();
}

void ThemeEditorWindow::load_text()
{
    std::ifstream file(theme_file, std::ios::binary);
    std::stringstream content;
    content << file.rdbuf();
    try {
        const auto buffer = text_view.get_buffer();
        // Replacing the same text would move the cursor back to the beginning
        if (buffer->get_text() != content.str()) {
            text_changed.block();
            buffer->set_text(content.str());
            text_changed.unblock();
        }
    } catch (const Glib::Error &e) {
        logger.error("Cannot show the theme file in the editor: {}", e.what());
    }
}

void ThemeEditorWindow::save_text()
{
    try {
        const std::string text = text_view.get_buffer()->get_text();
        std::ofstream file(theme_file, std::ios::binary | std::ios::trunc);
        file << text;
        if (!file) {
            throw std::runtime_error("write error");
        }
        file.close();
        text_dirty = false;
        // This save is not an external change: remember its time, and refresh the preview now
        last_edit_time = fs::last_write_time(theme_file);
        refresh_preview();
    } catch (const std::exception &e) {
        logger.error("Cannot save the theme file {}: {}", theme_file.string(), e.what());
    }
}

void ThemeEditorWindow::build_item_list()
{
    // Every stats form of the default theme, and of the custom data sources (numeric ones have all the forms)
    std::vector<theme_items::CustomSource> custom;
    for (const auto &[name, source]: sensors_custom::custom_data_sources()) {
        custom.push_back({name, source->last_values().has_value()});
    }
    items = theme_items::available_items(config::THEME_DEFAULT, custom);

    for (size_t index = 0; index < items.size(); index++) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        auto check = Gtk::make_managed<Gtk::CheckButton>();
        auto label = Gtk::make_managed<Gtk::Label>(items[index].label);
        label->set_xalign(0);
        label->set_hexpand(true);
        row->append(*check);
        row->append(*label);
        item_list.append(*row);
        check->signal_toggled().connect([this, index] { on_item_toggled(index); });
        item_checks.push_back(check);
    }
    item_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    item_list.signal_row_selected().connect([this](Gtk::ListBoxRow *row) { on_item_selected(row); });
}

void ThemeEditorWindow::update_item_states()
{
    const auto theme = parse_yaml(text_view.get_buffer()->get_text());
    if (!theme) {
        return;  // Keep the boxes of the last valid text
    }
    // Setting a box does not change the text: the actions of the boxes are not run
    updating_checks = true;
    for (size_t i = 0; i < items.size(); i++) {
        item_checks[i]->set_active(theme_items::is_shown(*theme, items[i].path));
    }
    updating_checks = false;
}

void ThemeEditorWindow::replace_text(const std::string &text)
{
    const auto buffer = text_view.get_buffer();
    // Keep the cursor where it was: the text is replaced as a whole
    const int offset = buffer->get_iter_at_mark(buffer->get_insert()).get_offset();
    buffer->set_text(text);
    buffer->place_cursor(buffer->get_iter_at_offset(std::min(offset, buffer->get_char_count())));
}

ThemeEditorWindow::~ThemeEditorWindow()
{
    // The test thread uses the window: wait for it before the window is destroyed
    if (test_thread.joinable()) {
        test_thread.join();
    }
}

void ThemeEditorWindow::on_test_clicked()
{
    if (!test_settings || test_thread.joinable()) {
        return;  // One test at a time
    }
    test_button.set_sensitive(false);
    label_coord.set_text("Showing the theme on the display...");

    // The display of config.json that uses this theme, with its own driver (the preview of the editor is simulated).
    // Same steps as the system monitor when it starts a display, then one drawing of every stat, then the send.
    const config::DisplayConfig settings = *test_settings;
    const size_t index = test_index;
    test_thread = std::thread([this, settings, index] {
        try {
            Display screen(settings, index);
            screen.initialize_display();
            screen.display_static_images();
            screen.display_static_text();
            stats::draw_all_once(screen);
            scheduler::drain_queue(screen.update_queue);
            test_result = std::format("Theme {} shown once on the display {}", theme_name, settings.com_port);
        } catch (const std::exception &e) {
            logger.error("Cannot show the theme on the display: {}", e.what());
            test_result = std::format("Cannot show the theme on the display: {}", e.what());
        }
        test_done.emit();
    });
}

void ThemeEditorWindow::on_item_toggled(size_t index)
{
    if (updating_checks || index >= items.size()) {
        return;
    }
    const bool wanted = item_checks[index]->get_active();
    const auto &path = items[index].path;
    std::string text = text_view.get_buffer()->get_text();

    bool changed = false;
    if (wanted) {
        // Present but hidden: SHOW becomes True. Absent: the block is added with the default values
        const auto theme = parse_yaml(text);
        if (theme && theme_items::is_present(*theme, path)) {
            changed = theme_items::set_show(text, path, true);
        } else if (theme) {
            changed = theme_items::add_item(text, config::THEME_DEFAULT, path);
        }
    } else {
        // The block is kept, with SHOW False
        changed = theme_items::set_show(text, path, false);
    }

    if (!changed) {
        // The text could not be changed (not valid YAML): the box goes back to its previous state
        updating_checks = true;
        item_checks[index]->set_active(!wanted);
        updating_checks = false;
        return;
    }
    // The change is saved and the preview refreshed after a short pause (see on_text_changed)
    replace_text(text);
}

void ThemeEditorWindow::update_selected_zone()
{
    selected_zone.reset();
    const auto row = item_list.get_selected_row();
    if (row == nullptr || error_in_theme || row->get_index() < 0 ||
        static_cast<size_t>(row->get_index()) >= items.size()) {
        return;
    }
    const auto &path = items[static_cast<size_t>(row->get_index())].path;
    const auto theme = parse_yaml(text_view.get_buffer()->get_text());
    if (!theme || !theme_items::is_shown(*theme, path)) {
        return;
    }
    const auto block = theme_items::form_node(*theme, path);
    if (!block) {
        return;
    }

    // Area of the element in theme coordinates, as the program draws it
    const std::string form = path.back();
    const int x = config::get(*block, "X", 0), y = config::get(*block, "Y", 0);
    LcdComm::TextArea area{0, 0, 0, 0};
    if (form == "GRAPH" || form == "LINE_GRAPH") {
        const int width = config::get(*block, "WIDTH", 0), height = config::get(*block, "HEIGHT", 0);
        if (width <= 0 || height <= 0) {
            return;
        }
        area = {x, y, x + width, y + height};
    } else if (form == "RADIAL") {
        const int radius = config::get(*block, "RADIUS", 0);
        if (radius <= 0) {
            return;
        }
        area = {x - radius, y - radius, x + radius, y + radius};
    } else if (const int width = config::get(*block, "WIDTH", 0); width > 0) {
        // Text with a given size: one line of the font size if no height is given
        const int height = config::get(*block, "HEIGHT", 0);
        area = {x, y, x + width, y + (height > 0 ? height : config::get(*block, "FONT_SIZE", 10))};
    } else {
        // Text with the size of its content: the area really drawn by the last rendering
        const auto drawn = display().lcd->text_area(x, y);
        if (!drawn) {
            return;  // Not drawn by the program with the current values
        }
        area = *drawn;
    }
    if (area.right <= area.left || area.bottom <= area.top) {
        return;
    }

    // Preview coordinates: the preview shows the whole screen image at display_width() x display_height()
    const double scale = static_cast<double>(display_width()) / display().lcd->get_width();
    selected_zone = Zone{static_cast<int>(area.left * scale), static_cast<int>(area.top * scale),
                         static_cast<int>((area.right - area.left) * scale),
                         static_cast<int>((area.bottom - area.top) * scale)};
}

void ThemeEditorWindow::on_item_selected(Gtk::ListBoxRow *row)
{
    update_selected_zone();
    viewer_picture.queue_draw();
    if (row == nullptr || static_cast<size_t>(row->get_index()) >= items.size()) {
        return;
    }
    const auto theme = parse_yaml(text_view.get_buffer()->get_text());
    if (!theme) {
        return;
    }
    const auto line = theme_items::line_of(*theme, items[static_cast<size_t>(row->get_index())].path);
    if (!line) {
        return;  // The element is not in the text
    }
    const auto buffer = text_view.get_buffer();
    auto iter = buffer->get_iter_at_line(*line);
    buffer->place_cursor(iter);
    text_view.scroll_to(iter, 0.1);
    text_view.grab_focus();
}

void ThemeEditorWindow::on_text_changed()
{
    text_dirty = true;
    update_item_states();
    // Save once the typing pauses, so that the preview is not refreshed for each key
    save_timer.disconnect();
    save_timer = Glib::signal_timeout().connect(
        [this] {
            save_text();
            return false;  // one shot
        },
        SAVE_DELAY_MS);
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
    if (selected_zone) {
        // Blinking outline: the inverse of the LED color, then red
        if (blink_on) {
            cr->set_source_rgb((255 - led_color.r) / 255.0, (255 - led_color.g) / 255.0,
                               (255 - led_color.b) / 255.0);
        } else {
            cr->set_source_rgb(1.0, 0.0, 0.0);
        }
        cr->set_line_width(2);
        cr->rectangle(selected_zone->x + 1, selected_zone->y + 1, std::max(selected_zone->width - 2, 1),
                      std::max(selected_zone->height - 2, 1));
        cr->stroke();
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

/** @brief Entry point of the theme editor. */
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
    // The theme editor only edits the first display
    config::DISPLAYS.resize(1);
    config::CONFIG_DATA["config"]["HW_SENSORS"] = "STATIC";  // For theme editor always use stub data
    config::DISPLAYS.front().theme = theme_name;  // Theme is given as argument
    config::load_theme();

    // For theme editor, always use simulated LCD
    config::DISPLAYS.front().revision = "SIMU";

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
