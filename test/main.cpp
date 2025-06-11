#include <iostream>
#include <chrono>
#include <thread>

using namespace std::chrono;

void symbol_test()
{
    printf("symbol_test \n");
    while(true) {
        std::this_thread::sleep_for(milliseconds(1000));
    }
}

int main()
{
    symbol_test();
    return 1;
}