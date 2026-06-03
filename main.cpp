#include <iostream>

#include "Repl.h"

int main() {
    calc::Repl repl(std::cin, std::cout);
    repl.printBanner();
    return repl.run();
}
