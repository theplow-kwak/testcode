#include <iostream>

template <typename Type>
void HexDump(Type lvalue)
{
    std::cout << sizeof(lvalue) << " : " << lvalue << "  0x" << std::hex << lvalue << std::dec << " :";
    for(int i = 0; i < sizeof(lvalue); i++)
    {
        if(!(i%4))
            printf(" 0x");
        printf("%.02X", reinterpret_cast<unsigned char *>(&lvalue)[i]);
    }
    std::cout << "\n";
}

template <typename Type, size_t size>
void HexDump(Type const (&arr)[size])
{
    std::cout << size << " : " << arr << "  0x"  << std::hex << arr << std::dec << " :";
    for(int i = 0; i < sizeof(arr); i++)
    {
        if(!(i%4))
            printf(" 0x");
        printf("%.02X", arr[i]);
    }
    std::cout << "\n";
}

int main()
{
    size_t s_type = 0x1232;
    int i_type = 0x1234;
    long l_type = 0x12345678;
    long long ll_type = 0x12345;
    double d_type = 0x23120;
    long double ld_type = 0x121231;
    int64_t type_64 = 0x1231233;
    char c_type = 't';
    unsigned char c_arr[16] = "dasdf";
    unsigned char c_arr8[8] = "dasdf";

    HexDump(s_type);
    HexDump(i_type);
    HexDump(l_type);
    HexDump(ll_type);
    HexDump(d_type);
    HexDump(ld_type);
    HexDump(type_64);
    HexDump(c_type);
    HexDump(c_arr);
    HexDump(c_arr8);
}