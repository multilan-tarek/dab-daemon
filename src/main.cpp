#include <atomic>
#include <csignal>
#include <string_view>
#include "handler/main_handler.h"
#include "helper/logger.h"

std::atomic_bool running{true};

void signal_handler(const int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

int main(const int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string_view(argv[i]) == "--debug") {
            Logger::set_level(LogLevel::DEBUG);
        }
    }

    const Logger logger{"MAIN"};
    logger.info("dab_daemon starting");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const auto main_handler = new MainHandler();

    while (running) {
        main_handler->handle();
    }

    delete main_handler;
    logger.info("dab_daemon stopped");

    return 0;
}
