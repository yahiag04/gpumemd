#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << "Usage: gpumemd --help\n\n"
                     "GPU memory broker: v0.1 repository template.\n"
                     "Memory accounting and Unix socket IPC are not implemented yet.\n";
        return 0;
    }

    if (argc == 1) {
        std::cerr << "gpumemd: template only; broker startup is not implemented.\n"
                     "Run gpumemd --help for current capabilities.\n";
        return 1;
    }

    std::cerr << "gpumemd: unsupported arguments; only --help is available "
                 "in this template.\n";
    return 2;
}
