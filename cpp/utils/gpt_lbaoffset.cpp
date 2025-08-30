#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <libgen.h>
#include <mntent.h>
#include <limits.h>
#include <vector>
#include <sys/stat.h>
#include <cstdint>
#include <algorithm>

// 디바이스의 섹터 크기 구하기
int get_sector_size(const char *device)
{
    int fd = open(device, O_RDONLY);
    if (fd < 0)
    {
        perror("open device");
        return -1;
    }
    int sector_size = 0;
    if (ioctl(fd, BLKSSZGET, &sector_size) < 0)
    {
        perror("ioctl(BLKSSZGET)");
        close(fd);
        return -1;
    }
    close(fd);
    return sector_size;
}

// 파일이 속한 디바이스 (/dev/sdXN) 찾기
std::string find_device_for_file(const char *filepath)
{
    FILE *mnt = setmntent("/proc/self/mounts", "r");
    if (!mnt)
    {
        perror("setmntent");
        return "";
    }

    struct mntent *ent;
    std::string best_match;
    size_t best_len = 0;

    while ((ent = getmntent(mnt)) != NULL)
    {
        std::string dir(ent->mnt_dir);
        if (dir.size() > best_len &&
            strncmp(filepath, dir.c_str(), dir.size()) == 0 &&
            (filepath[dir.size()] == '/' || filepath[dir.size()] == '\0'))
        {
            best_len = dir.size();
            best_match = ent->mnt_fsname; // e.g., /dev/nvme0n1p1
        }
    }
    endmntent(mnt);
    return best_match;
}

// 파티션 시작 LBA 구하기 (/sys/block/.../start 읽기)
uint64_t get_partition_start_lba(const std::string &devnode)
{
    char realpath_buf[PATH_MAX];
    if (!realpath(devnode.c_str(), realpath_buf))
    {
        perror("realpath");
        return 0;
    }
    std::string realdev = realpath_buf; // e.g., /dev/nvme0n1p1

    // sysfs 경로 변환
    std::string base = basename(realpath_buf); // nvme0n1p1
    std::string sys_path = "/sys/class/block/" + base + "/start";

    std::ifstream ifs(sys_path);
    uint64_t start_lba = 0;
    ifs >> start_lba;
    return start_lba;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <file> <offset>" << std::endl;
        return 1;
    }

    const char *filename = argv[1];
    off_t offset = std::stoll(argv[2]);

    // 1) 파일이 위치한 block device 찾기
    std::string devnode = find_device_for_file(filename);
    if (devnode.empty())
    {
        std::cerr << "Failed to find device for file" << std::endl;
        return 1;
    }
    std::cout << "File is on device: " << devnode << std::endl;

    // 2) 섹터 크기
    int sector_size = get_sector_size(devnode.c_str());
    if (sector_size <= 0)
    {
        std::cerr << "Failed to get sector size" << std::endl;
        return 1;
    }

    // 3) 파티션 시작 LBA
    uint64_t start_lba = get_partition_start_lba(devnode);
    std::cout << "Partition start LBA: " << start_lba << std::endl;

    // 4) FIEMAP으로 물리 오프셋 가져오기
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror("open file");
        return 1;
    }

    // extent를 저장할 충분한 공간을 할당합니다.
    unsigned int max_extents = 16;
    struct fiemap *fmap = (struct fiemap *)calloc(1, sizeof(struct fiemap) + max_extents * sizeof(struct fiemap_extent));
    if (!fmap)
    {
        perror("calloc");
        close(fd);
        return 1;
    }

    fmap->fm_start = offset;
    fmap->fm_length = 4096;
    fmap->fm_flags = FIEMAP_FLAG_SYNC; // 필요한 경우 플래그를 설정합니다.
    fmap->fm_extent_count = max_extents;
    fmap->fm_flags |= FIEMAP_FLAG_SYNC;

    if (ioctl(fd, FS_IOC_FIEMAP, fmap) < 0)
    {
        perror("ioctl(FIEMAP)");
        close(fd);
        free(fmap);
        return 1;
    }

    if (fmap->fm_mapped_extents == 0)
    {
        std::cerr << "No extent found (sparse file or offset out of range?)" << std::endl;
        close(fd);
        free(fmap);
        return 1;
    }

    bool found = false;
    for (unsigned i = 0; i < fmap->fm_mapped_extents; i++)
    {
        struct fiemap_extent *e = &fmap->fm_extents[i];
        if (offset >= e->fe_logical && offset < (e->fe_logical + e->fe_length))
        {
            uint64_t lba_rel = e->fe_physical / sector_size;
            uint64_t lba_abs = lba_rel + start_lba;
            std::cout << "File offset " << offset
                      << " -> physical=" << e->fe_physical
                      << " bytes"
                      << " -> relative LBA=" << lba_rel
                      << " -> absolute LBA=" << lba_abs
                      << std::endl;
            found = true;
            break; // 일치하는 extent를 찾았으므로 루프를 종료합니다.
        }
    }

    if (!found)
    {
        std::cerr << "Offset not found in any extent" << std::endl;
    }

    close(fd);
    free(fmap);
    return 0;
}
