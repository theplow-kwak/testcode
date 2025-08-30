#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <system_error>
#include <cstring>
#include <sys/stat.h> // Include for struct stat
#include <cstdlib>    // Include for malloc/free

// 디스크 섹터 크기 (일반적으로 512 바이트)
constexpr int SECTOR_SIZE = 512;

// 파일 경로와 오프셋을 받아 LBA를 계산하는 함수
void get_lba(const char *filepath, off_t offset)
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Failed to open file");
    }

    // 파일의 논리 블록 크기 가져오기
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        close(fd);
        throw std::system_error(errno, std::generic_category(), "Failed to get file stats");
    }
    long long block_size = st.st_blksize;

    // FIEMAP 구조체 준비 extent를 저장할 충분한 공간을 할당합니다.
    unsigned int max_extents = 1; // 필요한 extent의 최대 개수를 설정합니다.
    struct fiemap *fiemap_data = (struct fiemap *)malloc(sizeof(struct fiemap) + max_extents * sizeof(struct fiemap_extent));
    if (!fiemap_data)
    {
        close(fd);
        throw std::system_error(errno, std::generic_category(), "Failed to allocate memory for fiemap");
    }
    memset(fiemap_data, 0, sizeof(struct fiemap));

    fiemap_data->fm_start = offset;
    fiemap_data->fm_length = 1; // 오프셋이 포함된 1바이트만 확인
    fiemap_data->fm_flags = FIEMAP_FLAG_SYNC;
    fiemap_data->fm_extent_count = max_extents; // 요청하는 extent의 개수를 설정

    if (ioctl(fd, FS_IOC_FIEMAP, fiemap_data) < 0)
    {
        close(fd);
        free(fiemap_data);
        throw std::system_error(errno, std::generic_category(), "ioctl(FS_IOC_FIEMAP) failed to get extent count");
    }

    if (fiemap_data->fm_mapped_extents == 0)
    {
        std::cout << "Offset " << offset << " is not mapped to any physical block (sparse file?)." << std::endl;
        close(fd);
        free(fiemap_data);
        return;
    }

    // 결과 출력
    if (fiemap_data->fm_mapped_extents > 0)
    {
        const struct fiemap_extent *extent = &fiemap_data->fm_extents[0];
        unsigned long long logical_block_offset_in_extent = (offset - extent->fe_logical) / block_size;
        unsigned long long physical_block_address_bytes = extent->fe_physical + (logical_block_offset_in_extent * block_size);
        unsigned long long lba = physical_block_address_bytes / SECTOR_SIZE;

        std::cout << "File: " << filepath << std::endl;
        std::cout << "Offset: " << offset << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "File System Block Size: " << block_size << " bytes" << std::endl;
        std::cout << "Physical Block Address: " << physical_block_address_bytes << " (bytes)" << std::endl;
        std::cout << "Disk LBA (Logical Block Address): " << lba << std::endl;
    }
    else
    {
        std::cout << "Could not find a mapped extent for offset " << offset << "." << std::endl;
    }

    free(fiemap_data);
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <file_path> <offset>" << std::endl;
        return 1;
    }

    const char *filepath = argv[1];
    off_t offset = std::stoll(argv[2]);

    try
    {
        get_lba(filepath, offset);
    }
    catch (const std::system_error &e)
    {
        std::cerr << "Error: " << e.what() << " (code: " << e.code() << ")" << std::endl;
        return 1;
    }

    return 0;
}