#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

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

int main()
{
    std::vector<uint8_t> bin_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x12, 0x7C};

    std::string decimal_str = bin128_to_decimal(bin_data);
    std::cout << "Decimal: " << decimal_str << std::endl;

    return 0;
}
