// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
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
 * @file platform.h
 * @brief Operating system services that differ between Linux and Windows, behind a common API.
 */

#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace platform {

/** @brief Suffix of the executable files: ".exe" on Windows, empty on Linux. */
#ifdef _WIN32
inline constexpr const char *EXECUTABLE_SUFFIX = ".exe";
#else
inline constexpr const char *EXECUTABLE_SUFFIX = "";
#endif

/** @brief Absolute path of the running program, empty if it cannot be found. */
std::filesystem::path executable_path();

/**
 * @brief Path of another program of this project, built in the same directory as the running program (build/bin/).
 * @param name Name of the program, without suffix (for example "configure").
 * @return The path, with EXECUTABLE_SUFFIX. In the current directory if the running program cannot be found.
 */
std::filesystem::path program_path(const std::string &name);

/** @brief Identifier of the running process. */
int process_id();

/** @brief True if this program runs with administrator rights (elevated on Windows, root on Linux). */
bool is_elevated();

/** @brief Time elapsed since the system was started, in seconds. */
double uptime_seconds();

/** @brief Local time of t (thread-safe localtime). */
std::tm local_time(std::time_t t);

#ifdef _WIN32
/** @brief Conversion of a UTF-16 string of the Windows API to UTF-8. */
std::string to_utf8(const wchar_t *text);
#endif

/** @brief Names of the network interfaces of the system, without duplicates. */
std::vector<std::string> network_interface_names();

/** @brief Start the socket library of the system (needed by Windows before any socket call). Can be called again. */
void init_sockets();

/**
 * @brief Load a shared library (.so on Linux, .dll on Windows) for the whole run of the program.
 * @return A handle for find_symbol(), or nullptr if the library cannot be loaded.
 */
void *load_library(const char *name);

/** @brief Address of a function of a library loaded by load_library(), nullptr if not found. */
void *find_symbol(void *library, const char *name);

/**
 * @brief Start a program without waiting for it.
 * @param program Path of the program.
 * @return False if the program cannot be started.
 */
bool start_program(const std::filesystem::path &program);

/**
 * @brief Prepare the stop requests of this process: Ctrl+C, SIGTERM and SIGQUIT on Linux (blocked in all the
 * threads created afterwards); on Windows Ctrl+C, closing of the console, end of the session (logoff, shutdown) and
 * stop requests from send_stop_request().
 * Call it once, in the main thread, before creating other threads.
 */
void prepare_stop_requests();

/**
 * @brief Wait for a stop request (prepare_stop_requests() must have been called).
 * @return The reason of the stop, to be logged (for example "Caught signal 15"), or an empty string if the stop was
 * requested by this program itself with request_stop().
 */
std::string wait_for_stop_request();

/** @brief Ask this program to stop: wait_for_stop_request() returns. */
void request_stop();

/**
 * @brief Ask another program to stop cleanly (SIGTERM on Linux, the stop event of the process on Windows).
 * @param pid Identifier of the process, which called prepare_stop_requests().
 * @return False if the request cannot be sent.
 */
bool send_stop_request(int pid);

}  // namespace platform
