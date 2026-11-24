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
 * @file theme-editor.h
 * @brief Edit themes for System Monitor in a preview window on the computer. The preview window is refreshed as soon as the theme file is modified.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include <gtkmm.h>

#include "library/config.h"
#include "library/lcd/color.h"
#include "library/theme_items.h"

/**
 * @brief Window of the theme editor: the theme.yaml text is edited on the left, and a preview of the theme is shown on
 * the right. The text is saved automatically while editing, and the preview is refreshed after each save.
 */
class ThemeEditorWindow : public Gtk::ApplicationWindow
{
public:
    ThemeEditorWindow();
    ~ThemeEditorWindow() override;

private:
    /** @brief Load the theme and render it on the simulated display. Returns false if the theme contains errors. */
    bool refresh_theme();

    /** @brief Show the content of theme.yaml in the text editor, if it differs from the text being edited. */
    void load_text();

    /** @brief Refresh the preview from theme.yaml, and save preview.png in the theme folder. */
    void refresh_preview();

    /** @brief Load theme.yaml after a change made outside the editor (no unsaved change in the editor). */
    void reload_from_file(std::filesystem::file_time_type edit_time);

    /** @brief Ask whether the editor changes or the file changed outside the editor are kept. */
    void ask_reload(std::filesystem::file_time_type edit_time);

    /** @brief Create the list of display elements: one row per measure form, with a checkbox. */
    void build_item_list();

    /**
     * @brief Check the boxes of the elements shown in the text (present with SHOW True). Keeps the last state if the
     * text is not valid YAML.
     */
    void update_item_states();

    /** @brief Action of a box: show the element (add it if absent) or hide it (SHOW False), in the text. */
    void on_item_toggled(size_t index);

    /** @brief Replace the whole text, keeping the cursor at its offset. */
    void replace_text(const std::string &text);

    /** @brief Show the theme once on the real screen of the display that uses it (Test Hotscreen button). */
    void on_test_clicked();

    /** @brief Scroll the text to the first line of the selected element, and place the cursor there. */
    void on_item_selected(Gtk::ListBoxRow *row);

    /**
     * @brief Zone of the selected element in the preview, if it is checked (in the text) and shown (SHOW True). The
     * zone of a text is the area really drawn by the program. Empty if the text is not valid.
     */
    void update_selected_zone();

    /** @brief Write the text being edited to theme.yaml. The preview is refreshed by check_theme_file. */
    void save_text();

    /** @brief Called on every change of the text: the text is saved after a short pause in the typing. */
    void on_text_changed();

    /** @brief Preview picture from the simulated display (or the error picture), background colors from the theme */
    void update_preview();

    /** @brief Apply the zoom level to the preview and window size */
    void apply_zoom();

    /** @brief Every time the theme file is modified: reload preview */
    bool check_theme_file();

    void on_draw(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height);
    void on_drag_begin(double x, double y);
    void on_drag_update(double offset_x, double offset_y);
    void on_drag_end(double offset_x, double offset_y);
    void zoom(double delta);

    /** @brief Preview size in the window (display size / resize factor) */
    int display_width() const;
    int display_height() const;

    std::filesystem::path theme_file;
    std::filesystem::file_time_type last_edit_time;
    /** @brief True while the text has changes not saved to theme.yaml yet */
    bool text_dirty = false;
    /** @brief True while the dialog asking which version to keep is shown */
    bool asking_reload = false;
    Glib::RefPtr<Gtk::AlertDialog> reload_dialog;
    sigc::connection save_timer;
    sigc::connection text_changed;

    /** @brief Display elements of the list, in the order of the rows */
    std::vector<theme_items::Item> items;
    /** @brief Checkbox of each element of the list: checked if the element is shown in the text */
    std::vector<Gtk::CheckButton *> item_checks;
    /** @brief True while the boxes are set by the program: their actions are not run */
    bool updating_checks = false;
    double resize_factor = 1;
    bool error_in_theme = false;
    RGBColor led_color = {255, 255, 255};

    Glib::RefPtr<Gdk::Pixbuf> preview;  ///< full size
    Glib::RefPtr<Gdk::Pixbuf> scaled_preview;  ///< displayed size

    /** @brief Zone drawn with the mouse, in preview coordinates */
    struct Zone
    {
        int x, y, width, height;
    };
    std::optional<Zone> zone;
    int x0 = 0, y0 = 0;

    /** @brief Zone of the selected element of the list, drawn blinking (preview coordinates) */
    std::optional<Zone> selected_zone;
    /** @brief Current color of the blinking zone: the inverse of the LED color, or red */
    bool blink_on = true;
    sigc::connection blink_timer;

    Gtk::Box main_box{Gtk::Orientation::HORIZONTAL};
    Gtk::ScrolledWindow text_scroll;
    Gtk::TextView text_view;
    Gtk::ScrolledWindow items_scroll;
    Gtk::ListBox item_list;
    Gtk::Box box{Gtk::Orientation::VERTICAL};
    Gtk::DrawingArea viewer_picture;
    Gtk::Box buttons{Gtk::Orientation::HORIZONTAL};
    Gtk::Button zoom_plus_btn{"Zoom +"}, zoom_minus_btn{"Zoom -"};
    /** @brief Active only if the theme is used by a display of config.json */
    Gtk::Button test_button{"Test Screen"};
    /** @brief Settings of the display that uses the theme, and its index in config.json (if used) */
    std::optional<config::DisplayConfig> test_settings;
    size_t test_index = 0;
    /** @brief The test runs in its own thread, so that the window keeps responding while the screen receives the data */
    std::thread test_thread;
    /** @brief Emitted by the test thread when it is over: the window then shows test_result */
    Glib::Dispatcher test_done;
    std::string test_result;
    Gtk::Label label_coord{"Click or draw a zone to show coordinates"};
    Gtk::Label label_info{"This preview will reload when theme file is updated"};
    Glib::RefPtr<Gtk::CssProvider> css;
};
