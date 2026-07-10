/*
 * Minimal headless controller example.
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>
#include <tvision/headless.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tvision;

#ifndef _WIN32
static bool waitForFrame(THeadlessController &controller, uint64_t after,
                         int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        THeadlessNotification notification;
        std::string error;
        if (controller.receive(notification, 100, &error) &&
            notification.type == THeadlessNotificationType::Frame &&
            notification.state.sequence > after)
            return true;
    }
    return false;
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    std::cerr << "The sample controller currently supports Unix platforms.\n";
    return 1;
#else
    if (argc < 2)
    {
        std::cerr << "Usage: headless APPLICATION [capture.tvf]\n";
        return 2;
    }
    const char *capturePath = argc > 2 ? argv[2] : "capture.tvf";
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, channels) != 0)
        return 1;
    pid_t child = fork();
    if (child == 0)
    {
        close(channels[0]);
        std::string fd = std::to_string(channels[1]);
        setenv("TVISION_HEADLESS_FD", fd.c_str(), 1);
        setenv("TVISION_HEADLESS_SIZE", "100x30", 1);
        setenv("TVISION_HEADLESS_CELL", "10x20", 1);
        execl(argv[1], argv[1], static_cast<char *>(nullptr));
        _exit(127);
    }
    close(channels[1]);
    THeadlessController controller(channels[0]);
    if (!waitForFrame(controller, 0, 3000))
    {
        std::cerr << "application produced no frame\n";
        kill(child, SIGTERM);
        return 1;
    }

    // Control messages are ordinary events to the child: resize, a mouse
    // move and an atomic click, followed by a keyboard command.
    controller.resize({110, 32});
    controller.sendMouse(evMouseMove, {10, 5}, 0, 0, meMouseMoved);
    controller.sendMouse(evMouseDown, {10, 5}, mbLeftButton);
    controller.sendMouse(evMouseUp, {10, 5});
    controller.sendKey(kbF10);
    if (!waitForFrame(controller, 1, 3000))
        std::cerr << "warning: no redraw followed the example input\n";

    std::string error;
    if (!controller.capture(capturePath, &error))
    {
        std::cerr << error << '\n';
        kill(child, SIGTERM);
        return 1;
    }
    for (;;)
    {
        THeadlessNotification notification;
        if (!controller.receive(notification, 3000, &error))
            break;
        if (notification.type == THeadlessNotificationType::Capture)
        {
            if (notification.success)
                std::cout << "saved " << capturePath << '\n';
            else
                std::cerr << notification.message << '\n';
            break;
        }
    }
    kill(child, SIGTERM);
    while (waitpid(child, nullptr, 0) < 0) {}
    close(channels[0]);
    return 0;
#endif
}
