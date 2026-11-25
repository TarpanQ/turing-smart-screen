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
 * @file sensors_lhm.cpp
 * @brief CPU temperature and fan speed read with LibreHardwareMonitor, through the helper program lhm-bridge.
 */

#include "library/sensors/sensors_lhm.h"

#include <charconv>
#include <cmath>
#include <limits>

#ifdef _WIN32
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <mutex>

#include "library/config.h"
#include "library/log.h"
#include "library/platform.h"
#endif

namespace sensors_lhm {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

std::optional<Sensor> parse_sensor_line(const std::string &line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1;
    }
    if (fields.size() != 5 || fields[0].empty() || fields[2].empty()) {
        return std::nullopt;
    }
    // Written with the invariant culture of .NET: '.' as decimal separator, whatever the locale
    double value = NaN;
    const std::string &text = fields[4];
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc()) {
        value = NaN;
    }
    return Sensor{fields[0], fields[1], fields[2], fields[3], value};
}

double cpu_temperature(const std::vector<Sensor> &sensors)
{
    // Sensors of the first CPU only, like the Python version
    const std::string *cpu = nullptr;
    for (const Sensor &sensor: sensors) {
        if (sensor.hardware_type == "Cpu") {
            cpu = &sensor.hardware;
            break;
        }
    }
    if (!cpu) {
        return NaN;
    }
    // Average temperature of the cores, else the maximum, else the package (usually the maximum), else any core
    for (const char *prefix: {"Core Average", "Core Max", "CPU Package", "Core"}) {
        for (const Sensor &sensor: sensors) {
            if (sensor.hardware_type == "Cpu" && sensor.hardware == *cpu && sensor.type == "Temperature" &&
                sensor.name.starts_with(prefix) && !std::isnan(sensor.value)) {
                return sensor.value;
            }
        }
    }
    return NaN;
}

double cpu_fan_percent(const std::vector<Sensor> &sensors, const std::optional<std::string> &fan_name)
{
    // Fan controls of the motherboard (its Super I/O chips)
    const auto is_fan_control = [](const Sensor &sensor) {
        return sensor.hardware_type == "Motherboard" && sensor.type == "Control" && !std::isnan(sensor.value);
    };
    if (fan_name) {
        for (const Sensor &sensor: sensors) {
            if (is_fan_control(sensor) && sensor.hardware + "/" + sensor.name == *fan_name) {
                return sensor.value;
            }
        }
    }
    // Like the Python version: the fan #2 of the motherboard is usually the CPU fan
    for (const Sensor &sensor: sensors) {
        if (is_fan_control(sensor) && sensor.name.find("#2") != std::string::npos) {
            return sensor.value;
        }
    }
    return NaN;
}

#ifdef _WIN32

namespace {

/** @brief The lhm-bridge process, started once, and the last sensors it sent. */
class Bridge
{
public:
    static Bridge &instance()
    {
        static Bridge bridge;
        return bridge;
    }

    /** @brief The sensors of LibreHardwareMonitor, updated at most once per second; empty if not available. */
    std::vector<Sensor> sensors()
    {
        std::lock_guard lock(mutex);
        if (!started) {
            started = true;
            failed = !start();
        }
        if (failed) {
            return {};
        }
        const auto now = std::chrono::steady_clock::now();
        if (has_last && now - last_time < std::chrono::seconds(1)) {
            return last;
        }
        if (write_line("update")) {
            std::vector<Sensor> result;
            while (const auto line = read_line()) {
                if (*line == "end") {
                    last = result;
                    last_time = now;
                    has_last = true;
                    return result;
                }
                if (const auto sensor = parse_sensor_line(*line)) {
                    result.push_back(*sensor);
                }
            }
        }
        logger.error("LibreHardwareMonitor helper (lhm-bridge) stopped: temperatures and fans are not available");
        failed = true;
        return {};
    }

private:
    Bridge() = default;

    bool start()
    {
        const std::filesystem::path program = platform::program_path("lhm-bridge");
        const std::filesystem::path lhm_directory =
            config::find_main_directory() / "external" / "LibreHardwareMonitor";

        // Pipes for the standard input and output of the helper: only its ends are inherited
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE child_input = nullptr, child_output = nullptr;
        if (!CreatePipe(&child_input, &to_child, &inherit, 0)) {
            logger.error("Cannot start lhm-bridge: CreatePipe failed");
            return false;
        }
        if (!CreatePipe(&from_child, &child_output, &inherit, 0)) {
            CloseHandle(child_input);
            logger.error("Cannot start lhm-bridge: CreatePipe failed");
            return false;
        }
        SetHandleInformation(to_child, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(from_child, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = child_input;
        startup_info.hStdOutput = child_output;
        startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        std::wstring command_line = L"\"" + program.wstring() + L"\" \"" + lhm_directory.wstring() + L"\"";
        PROCESS_INFORMATION process_info{};
        const BOOL created = CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup_info, &process_info);
        CloseHandle(child_input);
        CloseHandle(child_output);
        if (!created) {
            logger.error("Cannot start {}: error {}", program.string(), GetLastError());
            return false;
        }
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);  // The helper exits by itself when this program closes its input

        // Opening the sensors can take a few seconds
        const auto first = read_line();
        if (!first || *first != "ready") {
            const std::string message = first && first->starts_with("error\t") ? first->substr(6) : "no answer";
            logger.error("LibreHardwareMonitor cannot be used ({}): temperatures and fans are not available", message);
            return false;
        }
        logger.info("LibreHardwareMonitor started (lhm-bridge)");
        return true;
    }

    bool write_line(const std::string &line)
    {
        const std::string data = line + "\n";
        DWORD written = 0;
        return WriteFile(to_child, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
               written == data.size();
    }

    std::optional<std::string> read_line()
    {
        while (true) {
            const size_t end = buffer.find('\n');
            if (end != std::string::npos) {
                std::string line = buffer.substr(0, end);
                buffer.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return line;
            }
            char chunk[4096];
            DWORD read = 0;
            if (!ReadFile(from_child, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
                return std::nullopt;  // The helper exited
            }
            buffer.append(chunk, read);
        }
    }

    std::mutex mutex;
    bool started = false;
    bool failed = false;
    HANDLE to_child = nullptr;
    HANDLE from_child = nullptr;
    std::string buffer;
    std::vector<Sensor> last;
    bool has_last = false;
    std::chrono::steady_clock::time_point last_time;
};

}  // namespace

double Cpu::temperature()
{
    return cpu_temperature(Bridge::instance().sensors());
}

double Cpu::fan_percent(const std::optional<std::string> &fan_name)
{
    return cpu_fan_percent(Bridge::instance().sensors(), fan_name);
}

bool is_available()
{
    std::error_code ec;
    return std::filesystem::is_regular_file(platform::program_path("lhm-bridge"), ec);
}

sensors::Sensors create()
{
    sensors::Sensors sensors = sensors_windows::create();
    sensors.cpu = std::make_unique<Cpu>();
    return sensors;
}

#endif

}  // namespace sensors_lhm
