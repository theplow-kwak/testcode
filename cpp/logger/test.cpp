#include "argparser.hpp"
#include "logger.hpp"

int main(int argc, char *argv[])
{
    Logger logger(LogLevel::Debug);
    ArgParser parser("샘플 프로그램: 파일을 입력받아 출력합니다.");
    parser.add_option("--file", "-f", "input file", true);
    parser.add_flag("--help", "-h", "show help");
    parser.add_positional("input", "input file", true);
    parser.add_positional("output", "output file", false);
    if (!parser.parse(argc, argv) || parser.is_set("--help") || parser.is_set("-h"))
    {
        parser.print_help(argv[0]);
        return 1;
    }
    auto file = parser.get("--file");
    if (file)
        logger.info("File: {}!", *file);
    auto input = parser.get_positional("input");
    auto output = parser.get_positional("output");
    if (input)
        logger.debug("Input: {}", *input);
    if (output)
        logger.warning("Output: {} - {}", *output, 3.14);

    logger.error("Error code: {}", -1);
}