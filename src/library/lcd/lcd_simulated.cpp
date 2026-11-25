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
 * @file lcd_simulated.cpp
 * @brief Simulated display, writing to a file (screencap.png) instead of a serial port.
 */

#include "library/lcd/lcd_simulated.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#include <system_error>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "library/log.h"
#include "library/platform.h"

namespace {

// The sockets are kept in an intptr_t: an int on Linux, a SOCKET on Windows (INVALID_SOCKET is then -1)
#ifdef _WIN32
SOCKET as_socket(intptr_t fd)
{
    return static_cast<SOCKET>(fd);
}

constexpr int SEND_FLAGS = 0;

std::string last_socket_error()
{
    return std::system_category().message(WSAGetLastError());
}

void close_socket(intptr_t fd)
{
    ::closesocket(as_socket(fd));
}

int poll_socket(intptr_t fd, int timeout_ms)
{
    WSAPOLLFD pfd{as_socket(fd), POLLIN, 0};
    return ::WSAPoll(&pfd, 1, timeout_ms);
}

intptr_t accept_client(intptr_t server_fd)
{
    return static_cast<intptr_t>(::accept(as_socket(server_fd), nullptr, nullptr));
}
#else
int as_socket(intptr_t fd)
{
    return static_cast<int>(fd);
}

constexpr int SEND_FLAGS = MSG_NOSIGNAL;

std::string last_socket_error()
{
    return std::strerror(errno);
}

void close_socket(intptr_t fd)
{
    ::close(as_socket(fd));
}

int poll_socket(intptr_t fd, int timeout_ms)
{
    pollfd pfd{as_socket(fd), POLLIN, 0};
    return ::poll(&pfd, 1, timeout_ms);
}

intptr_t accept_client(intptr_t server_fd)
{
    return ::accept4(as_socket(server_fd), nullptr, nullptr, SOCK_CLOEXEC);
}
#endif

void send_all(intptr_t fd, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        const auto n = ::send(as_socket(fd), data.data() + sent, static_cast<int>(data.size() - sent), SEND_FLAGS);
        if (n <= 0) {
            return;
        }
        sent += static_cast<size_t>(n);
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// SimulatedLcdWebServer

SimulatedLcdWebServer::SimulatedLcdWebServer(const std::string &host, int port)
{
    platform::init_sockets();
#ifdef _WIN32
    server_fd = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, 0));
#else
    server_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#endif
    if (server_fd < 0) {
        throw std::runtime_error(last_socket_error());
    }
#ifndef _WIN32
    // On Windows, SO_REUSEADDR would let another program use the same port: not needed to bind again at once
    const int reuse = 1;
    ::setsockopt(as_socket(server_fd), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, host == "localhost" ? "127.0.0.1" : host.c_str(), &addr.sin_addr);
    if (::bind(as_socket(server_fd), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        ::listen(as_socket(server_fd), 8) != 0) {
        const std::string error = last_socket_error();
        close_socket(server_fd);
        server_fd = -1;
        throw std::runtime_error(error);
    }

    thread = std::thread(&SimulatedLcdWebServer::serve_forever, this);
}

SimulatedLcdWebServer::~SimulatedLcdWebServer()
{
    shutdown();
}

void SimulatedLcdWebServer::shutdown()
{
    stopping = true;
    if (thread.joinable()) {
        thread.join();
    }
    if (server_fd >= 0) {
        close_socket(server_fd);
        server_fd = -1;
    }
}

void SimulatedLcdWebServer::serve_forever()
{
    while (!stopping) {
        // Wake up regularly to check if the server must stop
        if (poll_socket(server_fd, 250) <= 0) {
            continue;
        }
        const intptr_t client_fd = accept_client(server_fd);
        if (client_fd < 0) {
            continue;
        }
        handle_client(client_fd);
        close_socket(client_fd);
    }
}

void SimulatedLcdWebServer::handle_client(intptr_t client_fd)
{
    // Read the request line: "GET <path> HTTP/1.x"
    if (poll_socket(client_fd, 1000) <= 0) {
        return;
    }
    char buffer[2048];
    const auto n = ::recv(as_socket(client_fd), buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return;
    }
    buffer[n] = '\0';
    std::istringstream request(buffer);
    std::string method, path;
    request >> method >> path;

    if (path == "/") {
        const std::string screenshot = SCREENSHOT_FILE;
        const std::string body = "<img src=\"" + screenshot + "\" id=\"myImage\" />"
                                 "<script>"
                                 "setInterval(function() {"
                                 "    var myImageElement = document.getElementById('myImage');"
                                 "    myImageElement.src = '" + screenshot + "?rand=' + Math.random();"
                                 "}, 250);"
                                 "</script>";
        send_all(client_fd, "HTTP/1.0 200 OK\r\nContent-type: text/html\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body);
    } else if (path.rfind(std::string("/") + SCREENSHOT_FILE, 0) == 0) {
        std::ifstream file(SCREENSHOT_FILE, std::ios::binary);
        const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        send_all(client_fd, "HTTP/1.0 200 OK\r\nContent-type: image/png\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body);
    } else {
        send_all(client_fd, "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// LcdSimulated

LcdSimulated::LcdSimulated(const std::string &com_port, int display_width, int display_height,
                           UpdateQueue *update_queue, const std::string &screenshot_file, bool webserver)
    : LcdComm(com_port, display_width, display_height, update_queue), screenshot_file_name(screenshot_file)
{
    screen_image = Image(get_width(), get_height(), {255, 255, 255});
    save_screenshot();
    orientation = Orientation::PORTRAIT;

    if (!webserver) {
        return;
    }
    try {
        webServer = std::make_unique<SimulatedLcdWebServer>("localhost", WEBSERVER_PORT);
        logger.debug("To see your simulated screen, open http://{}:{} in a browser", "localhost", WEBSERVER_PORT);
    } catch (const std::runtime_error &) {
        logger.error("Error starting webserver! An instance might already be running on port {}.", WEBSERVER_PORT);
    }
}

LcdSimulated::~LcdSimulated()
{
    LcdSimulated::closeSerial();
}

std::optional<std::string> LcdSimulated::auto_detect_com_port()
{
    return std::nullopt;
}

void LcdSimulated::closeSerial()
{
    if (webServer) {
        logger.debug("Shutting down web server");
        webServer->shutdown();
        webServer.reset();
    }
}

void LcdSimulated::InitializeComm()
{
}

void LcdSimulated::Reset()
{
}

void LcdSimulated::Clear()
{
    SetOrientation(orientation);
}

void LcdSimulated::ScreenOff()
{
}

void LcdSimulated::ScreenOn()
{
}

void LcdSimulated::SetBrightness(int)
{
}

void LcdSimulated::SetBackplateLedColor(RGBColor)
{
}

void LcdSimulated::SetOrientation(Orientation new_orientation)
{
    orientation = new_orientation;
    // Just draw the screen again with the new width/height based on orientation
    std::lock_guard lock(update_queue_mutex);
    screen_image = Image(get_width(), get_height(), {255, 255, 255});
    save_screenshot();
}

void LcdSimulated::DisplayPILImage(const Image &input, int x, int y, int image_width, int image_height)
{
    Image image = input;

    // If the image height/width isn't provided, use the native image size
    if (!image_height) {
        image_height = image.height();
    }
    if (!image_width) {
        image_width = image.width();
    }

    // If image is bigger than display, crop it
    if (image.height() > get_height()) {
        image_height = get_height();
    }
    if (image.width() > get_width()) {
        image_width = get_width();
    }

    if (image_width != image.width() || image_height != image.height()) {
        image = image.crop(0, 0, image_width, image_height);
    }

    if (x > get_width()) {
        throw std::invalid_argument("Image X coordinate must be <= display width");
    }
    if (y > get_height()) {
        throw std::invalid_argument("Image Y coordinate must be <= display height");
    }
    if (image_height <= 0) {
        throw std::invalid_argument("Image height must be > 0");
    }
    if (image_width <= 0) {
        throw std::invalid_argument("Image width must be > 0");
    }

    std::lock_guard lock(update_queue_mutex);
    screen_image.paste(image, x, y);
    save_screenshot();
}

void LcdSimulated::save_screenshot()
{
    // Written to a temporary file first, so that the screenshot is never read while it is being written
    const std::string tmp_file = screenshot_file_name + ".tmp";
    screen_image.save(tmp_file);
    if (std::rename(tmp_file.c_str(), screenshot_file_name.c_str()) != 0) {
        logger.error("Cannot write {}: {}", screenshot_file_name, std::strerror(errno));
    }
}
