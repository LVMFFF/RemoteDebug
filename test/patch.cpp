#include <iostream>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

void __attribute__((constructor)) symbol_test_2()
{
    printf("patch func \n");
}