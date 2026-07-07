#include <cstdio>

extern "C" void shared_print() {
    printf("Hello from shared component!\n");
}