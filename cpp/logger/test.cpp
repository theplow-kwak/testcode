#include "argparser.hpp"
#include "logger.hpp"
#include <thread>

int main(int argc, char *argv[])
{
    Logger logger(LogLevel::DEBUG);
    ArgParser parser("Copy and Compare test. ver. 0.1.0");
    parser.add_option("--time", "-t", "test time (unit: min)");
    parser.add_option("--src", "-s", "source directory path", true);
    parser.add_option("--dest", "-d", "destination directory path", true);
    parser.add_option("--thread", "-T", "thread count", false, "1");
    parser.add_flag("--test", "", "for test. used time unit as minute");
    parser.add_option("--log", "-L", "log level", false, "INFO");
    if (!parser.parse(argc, argv))
    {
        return 1;
    }

    auto source = parser.get("src").value();
    std::vector<std::string> destlist = split(parser.get("dest").value(), ',');
    auto dest_count = destlist.size();
    auto multithread = std::stoi(parser.get("thread").value());
    auto test = parser.is_set("--test");
    auto nTestTime = std::stoi(parser.get("time").value_or("1")) * ((test) ? 1 : 60);
    auto log_level = parser.get("log").value();
    logger.set_level(log_level);

    logger.info("Source: {}", source);
    LOG_INFO(logger, "Destination: {}", parser.get("dest").value());
    logger.info("Thread count: {}", multithread);
    logger.info("Test mode: {}", test ? "enabled" : "disabled");
    logger.info("Test time: {} minutes", nTestTime);
    logger.info("Destination count: {}", dest_count);
    for (const auto &dest : destlist)
    {
        LOG_DEBUG(logger, "Destination path: {}", dest);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    logger.info("Starting copy and compare test...");

    logger.info("Copy and compare test completed.");
    return 0;
}