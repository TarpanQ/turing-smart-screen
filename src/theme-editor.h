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

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <gtkmm.h>

#include "library/lcd/color.h"

class ThemeEditorWindow : public Gtk::ApplicationWindow
{
public:
    ThemeEditorWindow();

private:
    // Load the theme and render it on the simulated display. Returns false if the theme contains errors.
    bool refresh_theme();

    // Preview picture from the simulated display (or the error picture), background colors from the theme
    void update_preview();

    // Apply the zoom level to the preview and window size
    void apply_zoom();

    // Every time the theme file is modified: reload preview
    bool check_theme_file();

    void on_draw(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height);
    void on_drag_begin(double x, double y);
    void on_drag_update(double offset_x, double offset_y);
    void on_drag_end(double offset_x, double offset_y);
    void zoom(double delta);

    // Preview size in the window (display size / resize factor)
    int display_width() const;
    int display_height() const;

    std::filesystem::path theme_file;
    std::filesystem::file_time_type last_edit_time;
    double resize_factor = 1;
    bool error_in_theme = false;
    RGBColor led_color = {255, 255, 255};

    Glib::RefPtr<Gdk::Pixbuf> preview;  // full size
    Glib::RefPtr<Gdk::Pixbuf> scaled_preview;  // displayed size

    // Zone drawn with the mouse, in preview coordinates
    struct Zone
    {
        int x, y, width, height;
    };
    std::optional<Zone> zone;
    int x0 = 0, y0 = 0;

    Gtk::Box box{Gtk::Orientation::VERTICAL};
    Gtk::DrawingArea viewer_picture;
    Gtk::Box buttons{Gtk::Orientation::HORIZONTAL};
    Gtk::Button zoom_plus_btn{"Zoom +"}, zoom_minus_btn{"Zoom -"};
    Gtk::Label label_coord{"Click or draw a zone to show coordinates"};
    Gtk::Label label_info{"This preview will reload when theme file is updated"};
    Glib::RefPtr<Gtk::CssProvider> css;
};
