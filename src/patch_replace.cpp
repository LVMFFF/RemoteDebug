#include <iostream>

void __attribute__((constructor)) init_func() {
    printf("replace func\n");
}