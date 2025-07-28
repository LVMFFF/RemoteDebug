#include <iostream>
#include <chrono>
#include <thread>

using namespace std::chrono;

void symbol_test()
{
    while(true) {
        printf("original func \n");
        std::this_thread::sleep_for(1s);
    }
}

int main()
{
    symbol_test();
    return 1;
}