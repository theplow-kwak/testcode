
#include <chrono>
#include <iostream>

using namespace std;

int main(void)
{
    cout << ratio<4,1000>::num << ',' << ratio<4,1000>::den << '\n';
}
