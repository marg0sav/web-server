#include "logger.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

void log_message(const std::string& filename, const std::string& message) {
    std::ofstream logfile(filename, std::ios::app);
    if (logfile.is_open()) {
        logfile << "web_server [" << getpid() << "]: " << message << std::endl;
    }
}
