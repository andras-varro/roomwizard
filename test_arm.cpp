#include <iostream>
#include <cstdio>

int main(int argc, char** argv) {
    printf("Hello from ARM!\n");
    printf("argc=%d\n", argc);
    if (argc > 1) {
        printf("argv[1]=%s\n", argv[1]);
    }
    std::cout << "C++ streams work too!" << std::endl;
    return 0;
}
