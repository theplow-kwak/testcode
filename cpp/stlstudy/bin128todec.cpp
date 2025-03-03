#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <fstream>

// 16바이트 바이너리 데이터를 10진수 문자열로 변환
std::string bin128_to_decimal(const std::vector<uint8_t> &bin)
{
    std::string decimal = "0"; // 초기값
    for (uint8_t byte : bin)
    {
        int carry = byte;
        for (int i = decimal.size() - 1; i >= 0; --i)
        {
            int num = (decimal[i] - '0') * 256 + carry;
            decimal[i] = '0' + (num % 10);
            carry = num / 10;
        }
        while (carry > 0)
        {
            decimal.insert(decimal.begin(), '0' + (carry % 10));
            carry /= 10;
        }
    }
    return decimal;
}

template <typename T>
void print_hex_dump(const std::vector<T>& data) {
    constexpr size_t bytes_per_row = 16;
    size_t byte_size = sizeof(T);
    size_t total_bytes = data.size() * byte_size;
    const uint8_t* byte_data = reinterpret_cast<const uint8_t*>(data.data());

    // Print header
    std::cout << "      ";
    for (size_t i = 0; i < bytes_per_row; i += byte_size) {
        std::cout << std::setw(byte_size * 2) << std::setfill(' ') << std::hex << i << " ";
    }
    std::cout << std::dec << "\n";
    
    for (size_t i = 0; i < total_bytes; i += bytes_per_row) {
        std::cout << std::setw(4) << std::setfill('0') << std::hex << i << ": ";
        for (size_t j = 0; j < bytes_per_row; j += byte_size) {
            if (i + j < total_bytes) {
                for (size_t k = 0; k < byte_size && i + j + k < total_bytes; ++k)
                    std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(byte_data[i + j + k]);
                std::cout << " ";
            } else {
                std::cout << std::string(byte_size * 2, ' ') << " ";
            }
        }
        std::cout << "\"";
        for (size_t j = 0; j < bytes_per_row && i + j < total_bytes; ++j) {
            char c = static_cast<char>(byte_data[i + j]);
            std::cout << (std::isprint(c) ? c : '.');
        }
        std::cout << "\"" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file)
    {
        std::cerr << "Unable to open file: " << argv[1] << std::endl;
        return 1;
    }

    // 시작 위치와 길이를 지정
    std::streampos start_pos = 0;
    std::streamsize length = 512; // 예시로 128바이트를 읽음

    // 파일의 시작 위치로 이동
    file.seekg(start_pos);

    // 지정된 길이만큼 읽기
    std::vector<uint8_t> bin_data_8(length);
    file.read(reinterpret_cast<char*>(bin_data_8.data()), length);
    file.close();

    std::vector<uint16_t> bin_data_16(bin_data_8.size() / 2);
    std::memcpy(bin_data_16.data(), bin_data_8.data(), bin_data_8.size());

    std::vector<uint32_t> bin_data_32(bin_data_8.size() / 4);
    std::memcpy(bin_data_32.data(), bin_data_8.data(), bin_data_8.size());

    // std::string decimal_str = bin128_to_decimal(bin_data);
    // std::cout << "Decimal: " << decimal_str << std::endl;
    print_hex_dump(bin_data_8);
    print_hex_dump(bin_data_16);
    print_hex_dump(bin_data_32);

    return 0;
}
