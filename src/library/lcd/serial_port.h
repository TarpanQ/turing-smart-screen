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
 * @file serial_port.h
 * @brief Serial port (termios on Linux, Win32 communications API on Windows), providing the subset of pyserial needed
 * by the program.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

/** @brief Error opening/reading/writing the serial port (like serial.SerialException) */
class SerialException : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** @brief Timeout writing to the serial port (like serial.SerialTimeoutException) */
class SerialTimeoutException : public SerialException
{
public:
    using SerialException::SerialException;
};

/** @brief Serial port (termios on Linux, Win32 on Windows) used to communicate with the screens. */
class SerialPort
{
public:
    /**
     * @brief Open the port in raw 8N1 mode. Throws SerialException on error.
     * timeout: read/write timeout in seconds; rtscts: hardware flow control
     */
    SerialPort(const std::string &port, int baudrate, double timeout, bool rtscts);
    virtual ~SerialPort();

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    /** @brief Close the serial port. */
    virtual void close();

    /** @brief True if the serial port is open. */
    virtual bool is_open() const;

    /** @brief Name of the serial port, for example /dev/ttyACM0 (COM3 on Windows). */
    const std::string &port() const { return port_name; }

    /** @brief Write all data. Throws SerialTimeoutException / SerialException */
    virtual void write(const std::vector<uint8_t> &data);

    /** @brief Read up to size bytes, returns less if the timeout expires */
    virtual std::vector<uint8_t> read(size_t size);

    /** @brief Read until the timeout expires */
    virtual std::vector<uint8_t> readall();

    /** @brief Wait until all data is written */
    virtual void flush();

    /** @brief Discard data received and not read yet */
    virtual void reset_input_buffer();

    /** @brief Number of bytes received and not read yet */
    virtual size_t in_waiting();

protected:
    /** @brief For test doubles that do not open a real port */
    SerialPort() = default;

private:
    std::string port_name;
    double timeout;
#ifdef _WIN32
    /** @brief Wait for the end of an overlapped read or write, returns the number of bytes transferred. */
    size_t transfer(bool for_write, void *data, size_t size);

    void *handle = nullptr;  ///< HANDLE of the port, nullptr if closed
#else
    bool wait(bool for_write, double timeout_s);

    int fd = -1;
#endif
};

/** @brief Information about a serial port, like serial.tools.list_ports_common.ListPortInfo */
struct ListPortInfo
{
    std::string device;  ///< e.g. "/dev/ttyACM0", or "COM3" on Windows
    std::string name;  ///< e.g. "ttyACM0", or "COM3" on Windows
    std::string serial_number;  ///< USB serial number, empty if unknown
    std::optional<uint16_t> vid;  ///< USB vendor ID, if USB device
    std::optional<uint16_t> pid;  ///< USB product ID, if USB device
    std::string location;  ///< USB ports from the root hub to the device, e.g. "5.1.4"; empty if not a USB device
};

/** @brief Prefix of the COM_PORT values that designate a serial port by its USB location, e.g. "USB:5.1.4". */
inline constexpr const char *USB_LOCATION_PREFIX = "USB:";

/** @brief List the serial ports of the system (like serial.tools.list_ports.comports()) */
std::vector<ListPortInfo> comports();

/**
 * @brief Fill the USB vendor ID, product ID and serial number of info from a Windows device instance ID, for example
 * "USB\VID_1A86&PID_5722\0000001" or "FTDIBUS\VID_0403+PID_6001+A50285BIA\0000" (used by comports() on Windows).
 * Nothing is changed if the ID is not the one of a USB device. The serial number is empty if Windows generated it.
 */
void parse_device_instance_id(const std::string &instance_id, ListPortInfo &info);

/**
 * @brief USB location of a device from its Windows location path, for example "5.1.4" from
 * "PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(5)#USB(1)#USB(4)" (port 5 of the root hub, port 1 of a hub, port 4).
 * Empty if the path is not the one of a USB device.
 */
std::string usb_location_from_location_path(const std::string &path);

/**
 * @brief USB location of a device from the name of its Linux sysfs directory, for example "5.1.4" from "1-5.1.4"
 * (bus 1). Empty if the name is not the one of a USB device.
 */
std::string usb_location_from_sysfs_name(const std::string &name);

/**
 * @brief Name of a port for COM_PORT: "USB:<location>" for a USB device (it does not change when the COM port numbers
 * change), its device otherwise.
 */
std::string port_config_name(const ListPortInfo &port);

/**
 * @brief Device of a configured port (COM_PORT other than AUTO): for "USB:<location>", the device of the port at this
 * USB location among ports (std::nullopt if no port is there); otherwise the configured port, unchanged.
 */
std::optional<std::string> resolve_port(const std::string &configured, const std::vector<ListPortInfo> &ports);
