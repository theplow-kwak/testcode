#include "argparser.hpp"
#include "logger.hpp"

int main(int argc, char *argv[])
{
    Logger logger(LogLevel::Debug);
    ArgParser parser("Copy and Compare test. ver. 0.1.0");
    parser.add_option("--time", "-t", "test time (unit: min)");
    parser.add_option("--src", "-s", "source directory path", true);
    parser.add_option("--dest", "-d", "destination directory path", true);
    parser.add_option("--thread", "-T", "thread count", false, "1");
    parser.add_flag("--test", "", "for test. used time unit as minute");
    if (!parser.parse(argc, argv))
    {
        return 1;
    }
    auto source = parser.get("src").value();
    std::vector<std::string> destlist = split(parser.get("dest").value(), ',');
    auto dest_count = destlist.size();
    auto multithread = std::stoi(parser.get("thread").value());
    auto test = std::stoi(parser.get("test").value_or("0"));
    auto nTestTime = std::stoi(parser.get("time").value_or("1")) * ((test) ? 1 : 60);

    logger.info("Source: {}", source);
    logger.info("Destination: {}", parser.get("dest").value());
    logger.info("Thread count: {}", multithread);
    logger.info("Test mode: {}", test ? "enabled" : "disabled");
    logger.info("Test time: {} minutes", nTestTime);
    logger.info("Destination count: {}", dest_count);
    for (const auto &dest : destlist)
    {
        logger.info("Destination path: {}", dest);
    }
    logger.info("Starting copy and compare test...");
    // 여기에 복사 및 비교 로직을 추가합니다.
    logger.info("Copy and compare test completed.");
    return 0;
}