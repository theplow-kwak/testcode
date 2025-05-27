#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <unordered_set>
#include <iomanip>

class ArgParser
{
public:
    ArgParser(const std::string &desc = "") : description_(desc) {}

    void set_description(const std::string &desc) { description_ = desc; }

    void add_option(const std::string &long_name, const std::string &short_name = "", const std::string &help = "", bool required = false)
    {
        Option opt{help, required, std::nullopt, long_name, short_name};
        if (!long_name.empty())
            option_map_[long_name] = opt;
        if (!short_name.empty())
            option_map_[short_name] = opt;
    }

    void add_flag(const std::string &long_name, const std::string &short_name = "", const std::string &help = "")
    {
        if (!long_name.empty())
            flag_map_[long_name] = help;
        if (!short_name.empty())
            flag_map_[short_name] = help;
    }

    void add_positional(const std::string &name, const std::string &help = "", bool required = false)
    {
        positional_defs_.push_back(Positional{name, help, required, std::nullopt});
    }

    bool parse(int argc, char *argv[])
    {
        size_t pos_idx = 0;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (flag_map_.count(arg))
            {
                parsed_flags_.insert(arg);
            }
            else if (option_map_.count(arg))
            {
                if (i + 1 < argc)
                {
                    option_map_[arg].value = argv[++i];
                    // 동기화: long/short 모두 값 저장
                    if (!option_map_[arg].long_name.empty())
                        option_map_[option_map_[arg].long_name].value = option_map_[arg].value;
                    if (!option_map_[arg].short_name.empty())
                        option_map_[option_map_[arg].short_name].value = option_map_[arg].value;
                }
                else
                {
                    std::cerr << "Option '" << arg << "' requires a value.\n";
                    return false;
                }
            }
            else
            {
                if (pos_idx < positional_defs_.size())
                {
                    positional_defs_[pos_idx++].value = arg;
                }
                else
                {
                    positional_args_.push_back(arg);
                }
            }
        }
        // Check required options (long name만 체크)
        for (const auto &[name, opt] : option_map_)
        {
            if (!opt.long_name.empty() && opt.required && !opt.value.has_value())
            {
                std::cerr << "Missing required option: " << opt.long_name << "\n";
                return false;
            }
        }
        // Check required positional
        for (const auto &pos : positional_defs_)
        {
            if (pos.required && !pos.value.has_value())
            {
                std::cerr << "Missing required positional argument: " << pos.name << "\n";
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> get(const std::string &name) const
    {
        // '--', '-' 접두어 없이도 찾을 수 있도록 처리
        auto match = [&](const std::string &key, const Option &opt)
        {
            if (key == name)
                return true;
            if (opt.long_name == name || opt.short_name == name)
                return true;
            // 접두어 제거 후 비교
            auto strip = [](const std::string &s)
            {
                if (s.rfind("--", 0) == 0)
                    return s.substr(2);
                if (s.rfind("-", 0) == 0)
                    return s.substr(1);
                return s;
            };
            return strip(opt.long_name) == name || strip(opt.short_name) == name || strip(key) == name;
        };
        for (const auto &[k, v] : option_map_)
        {
            if (match(k, v) && v.value.has_value())
                return v.value;
        }
        return std::nullopt;
    }

    bool is_set(const std::string &name) const
    {
        return parsed_flags_.count(name) > 0;
    }

    const std::vector<std::string> &positional() const
    {
        return positional_args_;
    }

    std::optional<std::string> get_positional(const std::string &name) const
    {
        for (const auto &pos : positional_defs_)
        {
            if (pos.name == name && pos.value.has_value())
                return pos.value;
        }
        return std::nullopt;
    }

    void print_help(const std::string &prog_name) const
    {
        if (!description_.empty())
            std::cout << description_ << "\n";
        std::cout << "Usage: " << prog_name;
        for (const auto &pos : positional_defs_)
        {
            std::cout << " <" << pos.name << ">";
        }
        std::cout << " [options] [args...]\n";
        // --- Positional arguments ---
        if (!positional_defs_.empty())
        {
            std::cout << "Positional arguments:\n";
            size_t maxlen = 0;
            for (const auto &pos : positional_defs_)
                maxlen = std::max(maxlen, pos.name.size());
            for (const auto &pos : positional_defs_)
            {
                std::cout << "  " << std::left << std::setw(static_cast<int>(maxlen) + 2) << pos.name
                          << pos.help;
                if (pos.required)
                    std::cout << " (required)";
                std::cout << "\n";
            }
        }
        // --- Options & Flags ---
        std::cout << "Options:\n";
        std::unordered_set<std::string> printed;
        std::vector<std::pair<std::string, std::string>> all_list;
        size_t maxlen = 0;
        // 옵션
        for (const auto &[name, opt] : option_map_)
        {
            if (!opt.long_name.empty() && printed.insert(opt.long_name).second)
            {
                std::string optstr = (opt.short_name.empty() ? "" : opt.short_name + ", ") + opt.long_name + " <value>";
                std::string desc = opt.help + (opt.required ? " (required)" : "");
                all_list.emplace_back(optstr, desc);
                maxlen = std::max(maxlen, optstr.size());
            }
        }
        // 플래그
        printed.clear();
        for (const auto &[name, help] : flag_map_)
        {
            if (name.size() == 2 && name[0] == '-' && printed.insert(name).second)
                continue;
            std::string short_flag;
            for (const auto &[n, h] : flag_map_)
            {
                if (n.size() == 2 && n[0] == '-' && h == help)
                    short_flag = n;
            }
            std::string flagstr = (short_flag.empty() ? "" : short_flag + ", ") + name;
            all_list.emplace_back(flagstr, help);
            maxlen = std::max(maxlen, flagstr.size());
        }
        // 출력
        for (const auto &p : all_list)
        {
            std::cout << "  " << std::left << std::setw(static_cast<int>(maxlen) + 2) << p.first << p.second << "\n";
        }
    }

private:
    struct Option
    {
        std::string help;
        bool required;
        std::optional<std::string> value;
        std::string long_name;
        std::string short_name;
    };
    struct Positional
    {
        std::string name;
        std::string help;
        bool required;
        std::optional<std::string> value;
    };
    std::string description_;
    std::unordered_map<std::string, Option> option_map_;
    std::unordered_map<std::string, std::string> flag_map_;
    std::vector<std::string> positional_args_;
    std::unordered_set<std::string> parsed_flags_;
    std::vector<Positional> positional_defs_;
};

// 사용 예시 (main 함수 등에서)
// int main(int argc, char* argv[]) {
//     ArgParser parser("샘플 프로그램: 파일을 입력받아 출력합니다.");
//     parser.add_option("--file", "-f", "input file", true);
//     parser.add_flag("--help", "-h", "show help");
//     parser.add_positional("input", "input file", true);
//     parser.add_positional("output", "output file", false);
//     if (!parser.parse(argc, argv) || parser.is_set("--help") || parser.is_set("-h")) {
//         parser.print_help(argv[0]);
//         return 1;
//     }
//     auto file = parser.get("--file");
//     if (file) std::cout << "File: " << *file << std::endl;
//     auto input = parser.get_positional("input");
//     auto output = parser.get_positional("output");
//     if (input) std::cout << "Input: " << *input << std::endl;
//     if (output) std::cout << "Output: " << *output << std::endl;
// }