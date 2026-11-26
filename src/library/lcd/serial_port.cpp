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
 * @file serial_port.cpp
 * @brief Serial port (termios on Linux, Win32 communications API on Windows), providing the subset of pyserial needed
 * by the program.
 */

#include "library/lcd/serial_port.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <regex>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "library/platform.h"

namespace fs = std::filesystem;

#ifdef _WIN32

namespace {

// Device setup classes of the serial ports: "Ports (COM & LPT)" and "Modem"
constexpr GUID PORTS_CLASS = {0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
constexpr GUID MODEM_CLASS = {0x4d36e96d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

std::string last_error_message()
{
    return std::system_category().message(static_cast<int>(GetLastError()));
}

// "COM10" and above can only be opened with the device path "\\.\COM10": used for all the ports, like pyserial
std::string device_path(const std::string &port)
{
    return port.starts_with("\\\\.\\") ? port : "\\\\.\\" + port;
}

DWORD to_milliseconds(double seconds)
{
    return seconds <= 0 ? 0 : static_cast<DWORD>(std::min(seconds * 1000, 4.0e9));
}

std::optional<std::string> device_instance_id(DEVINST devinst)
{
    wchar_t id[MAX_DEVICE_ID_LEN] = {};
    if (CM_Get_Device_IDW(devinst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
        return std::nullopt;
    }
    return platform::to_utf8(id);
}

// Name of the port of a device ("COM3"), from its registry key; empty if none
std::string port_name_of(HDEVINFO devices, SP_DEVINFO_DATA &device)
{
    HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) {
        return {};
    }
    wchar_t name[256] = {};
    DWORD size = sizeof(name) - sizeof(wchar_t);  // Room for the terminating null, not always stored
    DWORD type = 0;
    const LONG ret = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE *>(name), &size);
    RegCloseKey(key);
    return ret == ERROR_SUCCESS && type == REG_SZ ? platform::to_utf8(name) : std::string();
}

// First location path of a device ("PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(5)#..."), empty if none
std::string location_path_of(HDEVINFO devices, SP_DEVINFO_DATA &device)
{
    wchar_t paths[1024] = {};  // REG_MULTI_SZ: the first path ends with a NUL character
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_LOCATION_PATHS, nullptr,
                                           reinterpret_cast<BYTE *>(paths), sizeof(paths) - 2 * sizeof(wchar_t),
                                           nullptr)) {
        return {};
    }
    return platform::to_utf8(paths);
}

}  // namespace

SerialPort::SerialPort(const std::string &port, int baudrate, double timeout_s, bool rtscts)
    : port_name(port), timeout(timeout_s)
{
    // Exclusive access (no sharing), overlapped I/O so that a pending read does not block a write, like pyserial
    HANDLE h = CreateFileA(device_path(port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw SerialException("could not open port " + port + ": " + last_error_message());
    }
    handle = h;

    const auto fail = [&] {
        const std::string error = last_error_message();
        SerialPort::close();
        throw SerialException("could not configure port " + port + ": " + error);
    };

    SetupComm(h, 4096, 4096);

    // Reads return after the timeout with the bytes received; writes fail after the timeout. A timeout of 0 makes the
    // reads return at once (and the writes wait 1 ms, since 0 would mean no timeout)
    COMMTIMEOUTS timeouts{};
    if (timeout_s <= 0) {
        timeouts.ReadIntervalTimeout = MAXDWORD;
    } else {
        timeouts.ReadTotalTimeoutConstant = to_milliseconds(timeout_s);
    }
    timeouts.WriteTotalTimeoutConstant = std::max<DWORD>(1, to_milliseconds(timeout_s));
    if (!SetCommTimeouts(h, &timeouts)) {
        fail();
    }

    // Raw 8N1, DTR on, RTS on or hardware flow control (RTS/CTS)
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        fail();
    }
    dcb.BaudRate = static_cast<DWORD>(baudrate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = rtscts;
    dcb.fRtsControl = rtscts ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        fail();
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_RXABORT);
}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::is_open() const
{
    return handle != nullptr;
}

void SerialPort::close()
{
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
        handle = nullptr;
    }
}

size_t SerialPort::transfer(bool for_write, void *data, size_t size)
{
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        throw SerialException("CreateEvent failed: " + last_error_message());
    }
    const auto h = static_cast<HANDLE>(handle);
    const auto count = static_cast<DWORD>(size);
    BOOL ok = for_write ? WriteFile(h, data, count, nullptr, &overlapped)
                        : ReadFile(h, data, count, nullptr, &overlapped);
    DWORD transferred = 0;
    if (ok || GetLastError() == ERROR_IO_PENDING) {
        // Waits until the transfer is complete or the timeout of the port expires
        ok = GetOverlappedResult(h, &overlapped, &transferred, TRUE);
    }
    const DWORD error = GetLastError();
    CloseHandle(overlapped.hEvent);
    if (!ok && error != ERROR_OPERATION_ABORTED) {  // Aborted by reset_input_buffer(): returns what was transferred
        SetLastError(error);
        throw SerialException(std::string(for_write ? "write" : "read") + " failed: " + last_error_message() +
                              " (device disconnected or multiple access on port?)");
    }
    return transferred;
}

void SerialPort::write(const std::vector<uint8_t> &buffer)
{
    if (!handle) {
        throw SerialException("Attempting to use a port that is not open");
    }
    if (buffer.empty()) {
        return;
    }
    const size_t written = transfer(true, const_cast<uint8_t *>(buffer.data()), buffer.size());
    if (written != buffer.size()) {
        throw SerialTimeoutException("Write timeout");
    }
}

std::vector<uint8_t> SerialPort::read(size_t size)
{
    if (!handle) {
        throw SerialException("Attempting to use a port that is not open");
    }
    std::vector<uint8_t> result(size);
    if (size > 0) {
        result.resize(transfer(false, result.data(), size));
    }
    return result;
}

void SerialPort::flush()
{
    if (handle) {
        FlushFileBuffers(static_cast<HANDLE>(handle));
    }
}

size_t SerialPort::in_waiting()
{
    COMSTAT status{};
    DWORD errors = 0;
    if (!handle || !ClearCommError(static_cast<HANDLE>(handle), &errors, &status)) {
        return 0;
    }
    return status.cbInQue;
}

void SerialPort::reset_input_buffer()
{
    if (handle) {
        PurgeComm(static_cast<HANDLE>(handle), PURGE_RXCLEAR | PURGE_RXABORT);
    }
}

std::vector<ListPortInfo> comports()
{
    std::vector<ListPortInfo> ports;
    for (const GUID &device_class: {PORTS_CLASS, MODEM_CLASS}) {
        HDEVINFO devices = SetupDiGetClassDevsW(&device_class, nullptr, nullptr, DIGCF_PRESENT);
        if (devices == INVALID_HANDLE_VALUE) {
            continue;
        }
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &device); i++) {
            const std::string name = port_name_of(devices, device);
            if (name.empty() || name.starts_with("LPT")) {
                continue;  // Parallel ports are in the same class
            }
            ListPortInfo info;
            info.device = name;
            info.name = name;
            if (const auto id = device_instance_id(device.DevInst)) {
                parse_device_instance_id(*id, info);
            }
            info.location = usb_location_from_location_path(location_path_of(devices, device));

            // Interface of a composite USB device: the serial number is in the instance ID of a parent device
            DEVINST child = device.DevInst;
            DEVINST parent = 0;
            for (int depth = 0; info.vid && info.serial_number.empty() && depth < 3 &&
                                CM_Get_Parent(&parent, child, 0) == CR_SUCCESS;
                 depth++, child = parent) {
                ListPortInfo parent_info;
                if (const auto id = device_instance_id(parent)) {
                    parse_device_instance_id(*id, parent_info);
                }
                if (parent_info.vid != info.vid || parent_info.pid != info.pid) {
                    break;
                }
                info.serial_number = parent_info.serial_number;
            }

            ports.push_back(info);
        }
        SetupDiDestroyDeviceInfoList(devices);
    }

    std::sort(ports.begin(), ports.end(), [](const auto &a, const auto &b) { return a.device < b.device; });
    return ports;
}

#else  // Linux

namespace {

speed_t to_speed(int baudrate)
{
    switch (baudrate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        case 1000000: return B1000000;
        case 2000000: return B2000000;
        default: throw SerialException("Unsupported baudrate " + std::to_string(baudrate));
    }
}

std::string read_sysfs(const fs::path &path)
{
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}

}  // namespace

SerialPort::SerialPort(const std::string &port, int baudrate, double timeout_s, bool rtscts)
    : port_name(port.find('/') == std::string::npos ? "/dev/" + port : port), timeout(timeout_s)
{
    // A port name without directory (e.g. "ttyACM0", as written by the Python configuration program) is in /dev
    fd = ::open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        throw SerialException("could not open port " + port + ": " + std::strerror(errno));
    }

    // Exclusive access, like pyserial does on POSIX
    if (::ioctl(fd, TIOCEXCL) != 0) {
        const int err = errno;
        ::close(fd);
        fd = -1;
        throw SerialException("could not lock port " + port + ": " + std::strerror(err));
    }

    termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        const int err = errno;
        ::close(fd);
        fd = -1;
        throw SerialException("could not configure port " + port + ": " + std::strerror(err));
    }
    ::cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(CSTOPB | PARENB);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    if (rtscts) {
        tty.c_cflag |= CRTSCTS;
    } else {
        tty.c_cflag &= ~CRTSCTS;
    }
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    const speed_t speed = to_speed(baudrate);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);
    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        const int err = errno;
        ::close(fd);
        fd = -1;
        throw SerialException("could not configure port " + port + ": " + std::strerror(err));
    }
    ::tcflush(fd, TCIFLUSH);
}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::is_open() const
{
    return fd >= 0;
}

void SerialPort::close()
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool SerialPort::wait(bool for_write, double timeout_s)
{
    pollfd pfd{fd, static_cast<short>(for_write ? POLLOUT : POLLIN), 0};
    const int ret = ::poll(&pfd, 1, static_cast<int>(timeout_s * 1000));
    if (ret < 0) {
        if (errno == EINTR) {
            return false;
        }
        throw SerialException(std::string("poll failed: ") + std::strerror(errno));
    }
    if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        throw SerialException("device reports readiness to " + std::string(for_write ? "write" : "read") +
                              " but returned no data (device disconnected or multiple access on port?)");
    }
    return ret > 0;
}

void SerialPort::write(const std::vector<uint8_t> &buffer)
{
    const uint8_t *data = buffer.data();
    const size_t size = buffer.size();
    if (fd < 0) {
        throw SerialException("Attempting to use a port that is not open");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            throw SerialException(std::string("write failed: ") + std::strerror(errno));
        }
        const double remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0 || !wait(true, remaining)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                throw SerialTimeoutException("Write timeout");
            }
        }
    }
}

std::vector<uint8_t> SerialPort::read(size_t size)
{
    if (fd < 0) {
        throw SerialException("Attempting to use a port that is not open");
    }
    std::vector<uint8_t> result;
    result.reserve(size);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (result.size() < size) {
        const double remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0 || !wait(false, remaining)) {
            break;
        }
        uint8_t buffer[4096];
        const ssize_t n = ::read(fd, buffer, std::min(sizeof(buffer), size - result.size()));
        if (n > 0) {
            result.insert(result.end(), buffer, buffer + n);
        } else if (n == 0) {
            throw SerialException("device reports readiness to read but returned no data "
                                  "(device disconnected or multiple access on port?)");
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            throw SerialException(std::string("read failed: ") + std::strerror(errno));
        }
    }
    return result;
}

void SerialPort::flush()
{
    if (fd >= 0) {
        ::tcdrain(fd);
    }
}

size_t SerialPort::in_waiting()
{
    int count = 0;
    if (fd < 0 || ::ioctl(fd, FIONREAD, &count) != 0) {
        return 0;
    }
    return static_cast<size_t>(count);
}

void SerialPort::reset_input_buffer()
{
    if (fd >= 0) {
        ::tcflush(fd, TCIFLUSH);
    }
}

std::vector<ListPortInfo> comports()
{
    std::vector<ListPortInfo> ports;
    const fs::path tty_class("/sys/class/tty");
    std::error_code ec;
    if (!fs::exists(tty_class, ec)) {
        return ports;
    }

    for (const auto &entry: fs::directory_iterator(tty_class, ec)) {
        const std::string name = entry.path().filename().string();
        // Same device name patterns as pyserial on Linux
        static const char *patterns[] = {"ttyS", "ttyUSB", "ttyXRUSB", "ttyACM", "ttyAMA", "rfcomm", "ttyAP",
                                         "ttyGS"};
        if (std::none_of(std::begin(patterns), std::end(patterns),
                         [&](const char *p) { return name.rfind(p, 0) == 0; })) {
            continue;
        }

        // Only keep devices with a driver (like pyserial: ignores ttyS* placeholders without hardware)
        const fs::path device_path = entry.path() / "device";
        if (!fs::exists(device_path, ec)) {
            continue;
        }

        ListPortInfo info;
        info.device = "/dev/" + name;
        info.name = name;

        // USB devices: walk up the sysfs tree to the USB device holding idVendor/idProduct
        fs::path p = fs::canonical(device_path, ec);
        for (int depth = 0; !ec && depth < 6 && p.has_parent_path() && p != p.root_path(); depth++) {
            if (fs::exists(p / "idVendor", ec) && fs::exists(p / "idProduct", ec)) {
                try {
                    info.vid = static_cast<uint16_t>(std::stoi(read_sysfs(p / "idVendor"), nullptr, 16));
                    info.pid = static_cast<uint16_t>(std::stoi(read_sysfs(p / "idProduct"), nullptr, 16));
                } catch (const std::exception &) {
                    info.vid.reset();
                    info.pid.reset();
                }
                info.serial_number = read_sysfs(p / "serial");
                info.location = usb_location_from_sysfs_name(p.filename().string());
                break;
            }
            p = p.parent_path();
        }

        ports.push_back(info);
    }

    std::sort(ports.begin(), ports.end(), [](const auto &a, const auto &b) { return a.device < b.device; });
    return ports;
}

#endif

std::vector<uint8_t> SerialPort::readall()
{
    std::vector<uint8_t> result;
    while (true) {
        const auto chunk = read(4096);
        if (chunk.empty()) {
            break;
        }
        result.insert(result.end(), chunk.begin(), chunk.end());
    }
    return result;
}

void parse_device_instance_id(const std::string &instance_id, ListPortInfo &info)
{
    // Same patterns as pyserial: "USB\VID_1A86&PID_5722\0000001", "USB\VID_0525&PID_A4A7&MI_00\6&2C1F2B&0&0000",
    // "FTDIBUS\VID_0403+PID_6001+A50285BIA\0000"
    static const std::regex usb(R"(VID_([0-9A-F]{4})&PID_([0-9A-F]{4})(&MI_\d{2})?(\\(.*))?)", std::regex::icase);
    static const std::regex ftdi(R"(VID_([0-9A-F]{4})\+PID_([0-9A-F]{4})(\+(\w+))?)", std::regex::icase);
    std::smatch match;
    std::string serial_number;
    if (instance_id.starts_with("USB") && std::regex_search(instance_id, match, usb)) {
        serial_number = match[5];
    } else if (instance_id.starts_with("FTDIBUS") && std::regex_search(instance_id, match, ftdi)) {
        serial_number = match[4];
    } else {
        return;
    }
    info.vid = static_cast<uint16_t>(std::stoi(match[1], nullptr, 16));
    info.pid = static_cast<uint16_t>(std::stoi(match[2], nullptr, 16));
    // Windows generates an instance ID with '&' when the device has no usable serial number: not a serial number
    info.serial_number = serial_number.find('&') == std::string::npos ? serial_number : std::string();
}

std::string usb_location_from_location_path(const std::string &path)
{
    // Elements separated by '#': the ports are the "USB(n)" elements after "USBROOT(n)"
    std::string location;
    bool after_root = false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('#', start), path.size());
        const std::string element = path.substr(start, end - start);
        if (element.starts_with("USBROOT(")) {
            after_root = true;
        } else if (after_root && element.starts_with("USB(") && element.ends_with(")")) {
            const std::string port = element.substr(4, element.size() - 5);
            if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos) {
                return {};
            }
            location += (location.empty() ? "" : ".") + port;
        } else if (after_root) {
            break;  // Interface of a composite device (USBMI) or other element: end of the ports
        }
        start = end + 1;
    }
    return location;
}

std::string usb_location_from_sysfs_name(const std::string &name)
{
    // "<bus>-<port>.<port>...", without the ":<configuration>.<interface>" of the interfaces
    const size_t dash = name.find('-');
    if (dash == std::string::npos || dash == 0) {
        return {};
    }
    const std::string location = name.substr(dash + 1, name.find(':') == std::string::npos ? std::string::npos
                                                                                           : name.find(':') - dash - 1);
    if (location.empty() || location.find_first_not_of("0123456789.") != std::string::npos ||
        name.substr(0, dash).find_first_not_of("0123456789") != std::string::npos) {
        return {};
    }
    return location;
}

std::string port_config_name(const ListPortInfo &port)
{
    return port.location.empty() ? port.device : USB_LOCATION_PREFIX + port.location;
}

std::optional<std::string> resolve_port(const std::string &configured, const std::vector<ListPortInfo> &ports)
{
    if (!configured.starts_with(USB_LOCATION_PREFIX)) {
        return configured;
    }
    const std::string location = configured.substr(std::string(USB_LOCATION_PREFIX).size());
    for (const ListPortInfo &port: ports) {
        if (!port.location.empty() && port.location == location) {
            return port.device;
        }
    }
    return std::nullopt;
}
