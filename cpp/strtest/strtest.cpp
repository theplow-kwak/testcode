#include <algorithm>
#include <iterator>
#include <type_traits>
#include <functional>
#include <vector>
#include <iostream>
#include <iterator>
#include <string>

// std::vector<typename std::iterator_traits<InputIterator>::value_type>
template <
    class InputIterator,
    class Functor>
auto filter(InputIterator begin, InputIterator end, Functor f)
{
    using ValueType = typename std::iterator_traits<InputIterator>::value_type;

    std::vector<ValueType> result;
    result.reserve(std::distance(begin, end));

    std::copy_if(begin, end,
                 std::back_inserter(result),
                 f);
    return result;
}

template <
    class InputIterator,
    class Functor>
auto map(InputIterator begin, InputIterator end, Functor f)
{
    std::vector<int> functorValues;
    functorValues.reserve(unsigned(std::distance(begin, end)));

    std::transform(begin, end,
                   std::back_inserter(functorValues), f);

    return functorValues;
}

int main(int argc, char *argv[])
{
    std::vector<std::pair<int, int>> vec1 = {{1, 0}, {2, 0}, {3, 1}};

    for (auto &i : vec1)
    {
        std::cout << i.first << ':' << i.second << '\n';
    }

    auto vec2 = filter(vec1.begin(), vec1.end(),
                       [](auto f)
                       { return f.second == 1; });
    std::vector<int> vec3 = map(vec2.begin(), vec2.end(),
                   [](auto f) -> int
                   { return f.first; });

    for (auto &i : vec3)
    {
        std::cout << i << '\n';
    }
}
