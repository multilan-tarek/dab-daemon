#include <atomic>
#include <csignal>
#include "handler/main_handler.h"

std::atomic_bool running{true};

void signal_handler(const int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const auto main_handler = new MainHandler();

    while (running) {
        main_handler->handle();
    }

    delete main_handler;

    return 0;
}
