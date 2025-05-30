#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm> // For std::transform
#include <fstream>   // For std::ofstream

enum class LogLevel
{
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger
{
public:
    Logger(LogLevel level = LogLevel::INFO) : level_(level), file_stream_(nullptr) {}
    ~Logger()
    {
        if (file_stream_)
        {
            file_stream_->close();
            delete file_stream_;
        }
    }

    void set_level(LogLevel level)
    {
        level_ = level;
    }

    void set_level(const std::string &level_str)
    {
        std::string lvl = level_str;
        std::transform(lvl.begin(), lvl.end(), lvl.begin(), ::toupper);
        if (lvl == "TRACE")
            level_ = LogLevel::TRACE;
        else if (lvl == "DEBUG")
            level_ = LogLevel::DEBUG;
        else if (lvl == "INFO")
            level_ = LogLevel::INFO;
        else if (lvl == "WARNING")
            level_ = LogLevel::WARNING;
        else if (lvl == "ERROR")
            level_ = LogLevel::ERROR;
        else if (lvl == "FATAL")
            level_ = LogLevel::FATAL;
        else
            level_ = LogLevel::INFO;
    }

    // Set log file. If path is empty, log to stdout only.
    void set_logfile(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_stream_)
        {
            file_stream_->close();
            delete file_stream_;
            file_stream_ = nullptr;
        }
        if (!path.empty())
        {
            file_stream_ = new std::ofstream(path, std::ios::app);
        }
    }

    // Log with file name and line number (default: use macro)
    template <typename... Args>
    void log(LogLevel msg_level, const std::string &fmt_str, Args &&...args)
    {
        log_impl(msg_level, fmt_str, "", 0, std::forward<Args>(args)...);
    }

    // Log with file name and line number (used by macro)
    template <typename... Args>
    void log_impl(LogLevel msg_level, const std::string &fmt_str, const char *file, int line, Args &&...args)
    {
        if (msg_level < level_)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto duration = now.time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
        int micro3 = static_cast<int>(micros / 1000); // 3 digits
        std::ostringstream time_ss;
        time_ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        time_ss << "." << std::setfill('0') << std::setw(3) << micro3;
        std::string level_str = level_to_string(msg_level);
        std::string msg = format(fmt_str, std::forward<Args>(args)...);
        std::ostringstream out;
        if (file && line > 0)
            out << time_ss.str() << " [" << level_str << "][" << file << ":" << line << "] " << msg << std::endl;
        else
            out << time_ss.str() << " [" << level_str << "] " << msg << std::endl;
        // Output to file if set
        if (file_stream_ && file_stream_->is_open())
            *file_stream_ << out.str();
        std::cout << out.str();
    }

    // Macro for logging with file and line info
#define LOG_TRACE(logger, fmt, ...) (logger).log_impl(LogLevel::TRACE, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(logger, fmt, ...) (logger).log_impl(LogLevel::DEBUG, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(logger, fmt, ...) (logger).log_impl(LogLevel::INFO, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARNING(logger, fmt, ...) (logger).log_impl(LogLevel::WARNING, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(logger, fmt, ...) (logger).log_impl(LogLevel::ERROR, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_FATAL(logger, fmt, ...) (logger).log_impl(LogLevel::FATAL, fmt, __FILE__, __LINE__, ##__VA_ARGS__)

    template <typename... Args>
    void debug(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::DEBUG, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::INFO, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warning(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::WARNING, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::ERROR, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void trace(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::TRACE, fmt_str, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void fatal(const std::string &fmt_str, Args &&...args)
    {
        log(LogLevel::FATAL, fmt_str, std::forward<Args>(args)...);
    }

private:
    LogLevel level_;
    std::mutex mutex_;
    std::ofstream *file_stream_ = nullptr;

    // Convert LogLevel enum to string
    static std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    // Replace the first occurrence of 'from' with 'to' in 'str'
    static void replace_first(std::string &str, const std::string &from, const std::string &to)
    {
        size_t start_pos = str.find(from);
        if (start_pos != std::string::npos)
            str.replace(start_pos, from.length(), to);
    }

    // Format a string by replacing '{}' with arguments (recursively)
    template <typename T, typename... Args>
    static std::string format(const std::string &fmt_str, T &&value, Args &&...args)
    {
        std::ostringstream oss;
        oss << std::forward<T>(value);
        std::string result = fmt_str;
        replace_first(result, "{}", oss.str());
        if constexpr (sizeof...(args) > 0)
            return format(result, std::forward<Args>(args)...);
        else
            return result;
    }

    // Base case for format recursion
    static std::string format(const std::string &fmt_str)
    {
        return fmt_str;
    }
};

// int main()
// {
//     Logger logger(LogLevel::Debug);
//     logger.info("Hello, {}!", "world");
//     logger.debug("Value: {}", 42);
//     logger.warning("This is a warning: {}", 3.14);
//     logger.error("Error code: {}", -1);
// }