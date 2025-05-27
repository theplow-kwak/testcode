#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// 로그 레벨 정의
enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
public:
    Logger(LogLevel level = LogLevel::Info) : level_(level) {}

    void set_level(LogLevel level)
    {
        level_ = level;
    }

    template <typename... Args>
    void log(LogLevel msg_level, const std::string &fmt_str, Args &&...args)
    {
        if (msg_level < level_)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream time_ss;
        time_ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        std::string level_str = level_to_string(msg_level);
        std::string msg = format(fmt_str, std::forward<Args>(args)...);
        std::cout << "[" << time_ss.str() << "][" << level_str << "] " << msg << std::endl;
    }

    template <typename... Args>
    void debug(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::Debug, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::Info, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warning(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::Warning, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::Error, fmt_str, std::forward<Args>(args)...);
    }

private:
    LogLevel level_;
    std::mutex mutex_;
    std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }
    // 간단한 포매팅 함수: '{}'를 인자 순서대로 치환
    void replace_first(std::string &str, const std::string &from, const std::string &to)
    {
        size_t start_pos = str.find(from);
        if (start_pos != std::string::npos)
            str.replace(start_pos, from.length(), to);
    }
    template <typename T, typename... Args>
    std::string format(const std::string &fmt_str, T &&value, Args &&...args)
    {
        std::ostringstream oss;
        oss << std::forward<T>(value);
        std::string result = fmt_str;
        replace_first(result, "{}", oss.str());
        if constexpr (sizeof...(args) > 0)
        {
            return format(result, std::forward<Args>(args)...);
        }
        else
        {
            return result;
        }
    }
    std::string format(const std::string &fmt_str)
    {
        return fmt_str;
    }
};

// 사용 예시 (main 함수 등에서)
// int main() {
//     Logger logger(LogLevel::Debug);
//     logger.info("Hello, {}!", "world");
//     logger.debug("Value: {}", 42);
//     logger.warning("This is a warning: {}", 3.14);
//     logger.error("Error code: {}", -1);
// }