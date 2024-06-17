#include <algorithm>
#include <string>
#include <iostream>
#include <cctype>
using namespace std;

void lowerCase(char &c)
{
    c = tolower(static_cast<unsigned char>(c));
}

class Show
{
    int d_count;

    public:
        Show()
        :
            d_count(0)
        {}
        void operator()(std::string &str)
        {
            std::for_each(str.begin(), str.end(), lowerCase);
            str[0] = toupper(str[0]);   // assuming str is not empty
            std::cout << ++d_count << " " << str << "; ";
        }
        int count() const
        {
            return d_count;
        }
};

int main()
{
    string sarr[] =
        {
            "alpha", "BRAVO", "charley", "DELTA", "echo",
            "FOXTROT", "golf", "HOTEL"
        };
    string *last = sarr + sizeof(sarr) / sizeof(string);

    cout << for_each(sarr, last, Show()).count() << '\n';
}
