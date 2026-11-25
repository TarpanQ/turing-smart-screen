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
 * @brief Serial port (termios on Linux, not supported yet on Windows), providing the subset of pyserial needed by the
 * program.
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

/** @brief Serial port (termios) used to communicate with the screens. */
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
    virtual bool is_open() const { return fd >= 0; }

    /** @brief Name of the serial port, for example /dev/ttyACM0. */
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
    bool wait(bool for_write, double timeout_s);

    std::string port_name;
    double timeout;
    int fd = -1;
};

/** @brief Information about a serial port, like serial.tools.list_ports_common.ListPortInfo */
struct ListPortInfo
{
    std::string device;  ///< e.g. "/dev/ttyACM0"
    std::string name;  ///< e.g. "ttyACM0"
    std::string serial_number;  ///< USB serial number, empty if unknown
    std::optional<uint16_t> vid;  ///< USB vendor ID, if USB device
    std::optional<uint16_t> pid;  ///< USB product ID, if USB device
};

/** @brief List the serial ports of the system (like serial.tools.list_ports.comports()) */
std::vector<ListPortInfo> comports();
