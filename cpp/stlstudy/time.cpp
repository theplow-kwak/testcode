#include <iostream>
#include <chrono>
#include <iomanip>

using namespace std;

struct days: public chrono::hours
{
    days(size_t count)
    :
        chrono::hours(count * 24)
    {}
};

int main()
{
    chrono::time_point<chrono::system_clock>
                            timePoint{chrono::system_clock::now()};

    time_t time = chrono::system_clock::to_time_t(timePoint);

    cout << time << '\n';

    tm tmValue{*localtime(&time)};

    cout << put_time(&tmValue, "current time: %c") << '\n';

    cout << put_time(&tmValue, "rfs2822 format: %a, %e %b %Y %T %z")
                                                                << '\n';

    timePoint += days(7);

    time = chrono::system_clock::to_time_t(timePoint);

    cout << put_time(gmtime(&time),
                    "gmtime, one week from now:  %c %z") << '\n';

    time_t current_time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    cout << put_time(localtime(&current_time), "current_time: %Y_%m_%d") << '\n';
}
