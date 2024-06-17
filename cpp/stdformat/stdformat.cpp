#include <format>
#include <iostream>
#include <iterator>
#include <string>

#include "argparse\argparse.hpp"

struct Point {
    int x;
    int y;
};

// template<>
// struct std::formatter<Point>
// {
//     template <typename FormatParseContext>
//     auto parse(FormatParseContext& pc)
//     {
//         // parse formatter args like padding, precision if you support it
//         return pc.end(); // returns the iterator to the last parsed character in the format string, in this case we just swallow everything
//     }

//     template<typename FormatContext>
//     auto format(Point p, FormatContext& fc) 
//     {
//         return std::format_to(fc.out(), "[{}, {}]", p.x, p.y) ;
//     }
// };

template<>
struct std::formatter<Point> : std::formatter<std::string>
{
    template<typename Context>
    auto format(const Point point, Context& context)
    {
        format_to(context.out(), "[{}, ", point.x);
        return format_to(context.out(), " {}]", point.y);
        // return formatter<std::string>::format(std::format("[{}, {}]", point.x, point.y), context);
    }
};

int main(int argc, char *argv[])
{
    Point pt{3,4};
    std::string s = std::format("Hello {:>44}!\n", pt);
    printf("%s\n", s.c_str() ); 

    argparse::ArgumentParser program("test");

    program.add_argument("--verbose")
        .help("increase output verbosity")
        .default_value(false)
        .implicit_value(true);

    try {
    program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    std::exit(1);
    }

    if (program["--verbose"] == true) {
        std::cout << "Verbosity enabled" << std::endl;
    }

}