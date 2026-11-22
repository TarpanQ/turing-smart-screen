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
 * @file configure.h
 * @brief The system monitor configuration GUI (GTK 4 with gtkmm). Values are written to config.json, see README.config for their meaning.
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm.h>
#include <yaml-cpp/yaml.h>

#include "library/config.h"

/** @brief Drop-down list of strings (replaces ttk.Combobox in read-only mode) */
class StringDropDown : public Gtk::DropDown
{
public:
    StringDropDown();
    /** @brief Create a drop-down list with the given items. */
    explicit StringDropDown(const std::vector<std::string> &items);

    /** @brief Replace the items of the list. */
    void set_items(const std::vector<std::string> &items);
    /** @brief Items of the list. */
    std::vector<std::string> get_items() const;

    /**
     * @brief Select the item with this text. If the text is not in the list, it is added at the end (like ttk.Combobox,
     * which can display a value that is not in its list)
     */
    void set_text(const std::string &text);
    /** @brief Text of the selected item. */
    std::string get_text() const;

    /** @brief Index of the selected item, -1 if none */
    int current() const;
    /** @brief Select the item at the given index. */
    void set_current(int index);

private:
    Glib::RefPtr<Gtk::StringList> list;
};

/** @brief Controls of one display tab, holding the settings of one display of the displays array. */
struct DisplayTab
{
    DisplayTab();

    /** @brief Page of the tab, containing the controls. */
    Gtk::Fixed *page = nullptr;
    StringDropDown model_cb, size_cb, com_cb, orient_cb, theme_cb;
    Gtk::Scale brightness_slider;
    Gtk::Label brightness_val_label, brightness_warning_label;
};

class TuringConfigWindow;

/** @brief Sub-window for weather & ping configuration */
class MoreConfigWindow : public Gtk::Window
{
public:
    /** @brief Create the window of the additional settings (ping, weather). */
    explicit MoreConfigWindow(TuringConfigWindow &main_window);

    /** @brief Fill the fields with the values of the configuration. */
    void load_config_values(const config::Json &cfg);
    /** @brief Show the window. */
    void show_window();

private:
    void citysearch_show_warning(const std::string &warning);
    void on_search_click();
    void on_filllatlong_click();
    void on_save_click();
    void save_config_values();
    /** @brief Coordinates entries only accept a decimal number */
    void setup_coordinate_entry(Gtk::Entry &entry, std::string &last_valid);

    struct CityEntry
    {
        std::string full_name;
        std::string lat;
        std::string lon;
    };

    TuringConfigWindow &main_window;
    Gtk::Fixed layout;
    Gtk::Entry ping_entry, api_entry, lat_entry, long_entry, citysearch_entry;
    std::string lat_last_valid, long_last_valid;
    StringDropDown unit_cb, lang_cb, citysearch_cb;
    Gtk::Label citysearch_warn_label;
    std::vector<CityEntry> city_entries;
};

/** @brief Main window of the configuration program. */
class TuringConfigWindow : public Gtk::ApplicationWindow
{
public:
    TuringConfigWindow();

    /** @brief Save the additional settings (ping, weather) in config.json. */
    void save_additional_config(const std::string &ping, const std::string &api_key, const std::string &lat,
                                const std::string &lon, const std::string &unit, const std::string &lang);

private:
    void load_theme_preview();
    /** @brief The display tab currently selected. */
    DisplayTab &current_tab();
    /** @brief Add a tab with the settings of a display, after the existing tabs. */
    DisplayTab &add_display_tab(const config::DisplayConfig &display);
    /** @brief Set the controls of a tab to the settings of a display. */
    void apply_display(DisplayTab &tab, const config::DisplayConfig &display);
    /** @brief With several displays, a tab cannot use the automatic COM port detection. */
    void enforce_com_rule(DisplayTab &tab);
    /** @brief Name the tabs after their index (":0", ":1", ...). */
    void refresh_tab_labels();
    /** @brief Index of the tab under the position (in the notebook coordinates), -1 if none. */
    int tab_index_at(double x, double y);
    void on_tab_right_click(double x, double y);
    void on_add_display();
    void on_close_display();
    void load_config_values();
    /** @brief Returns false (with an error message) if the values cannot be saved */
    bool save_config_values();

    void on_theme_change(DisplayTab &tab);
    void on_open_theme_folder_click();
    void on_theme_editor_click();
    void on_save_click();
    void on_saverun_click();
    void on_brightness_change(DisplayTab &tab);
    void on_model_change(DisplayTab &tab);
    void on_size_change(DisplayTab &tab);
    void on_hwlib_change();
    void show_hide_brightness_warning(DisplayTab &tab);
    bool on_fan_speed_update();
    void show_error(const std::string &message);

    Gtk::Fixed layout;
    Gtk::Picture theme_preview;
    Gtk::Label theme_author;
    Gtk::Notebook notebook;
    std::vector<std::unique_ptr<DisplayTab>> tabs;
    Gtk::PopoverMenu *tab_menu = nullptr;
    Glib::RefPtr<Gio::SimpleAction> add_display_action, close_display_action;
    /** @brief Index of the tab that was right-clicked, -1 if none */
    int context_tab = -1;
    StringDropDown hwlib_cb, eth_cb, wl_cb, cpu_fan_cb;
    Gtk::Label cpu_fan_label;
    Gtk::Button weather_ping_btn, open_theme_folder_btn, edit_theme_btn, save_btn, save_run_btn;

    MoreConfigWindow more_config_window;
};
