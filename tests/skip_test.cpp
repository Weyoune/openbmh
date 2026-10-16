// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "SKIP: " << (argc > 1 ? argv[1] : "external test inputs unavailable") << '\n';
    return 77;
}
