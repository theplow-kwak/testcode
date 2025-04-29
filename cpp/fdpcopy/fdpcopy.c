#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/nvme_ioctl.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define NVME_FDP_COPY_OPCODE 0xC4 // 기본 가정 (디바이스 스펙에 따라 다를 수 있음)
#define MAX_COPY_ENTRIES 4        // 복사할 최대 엔트리 수

const char *decode_vendor_status(uint8_t sc)
{
    switch (sc)
    {
    case 0x80:
        return "Samsung: Internal Media Error";
    case 0x81:
        return "Samsung: Write Amplification Limit Reached";
    case 0x82:
        return "Samsung: Thermal Throttle Engaged";
    // 필요하면 추가: 다른 Vendor 코드
    default:
        return "Unknown Vendor Specific Error";
    }
}

// Copy Descriptor Entry (16 bytes)
struct copy_entry
{
    uint64_t slba;         // Source LBA
    uint32_t nlb;          // Number of LBAs (0-based)
    uint16_t reserved;     // Reserved
    uint16_t placement_id; // Placement ID
} __attribute__((packed));

const char *decode_nvme_status(uint8_t sct, uint8_t sc)
{
    if (sct == 0x0)
    { // Generic Command Status
        switch (sc)
        {
        case 0x00:
            return "Successful Completion";
        case 0x01:
            return "Invalid Command Opcode";
        case 0x02:
            return "Invalid Field in Command";
        case 0x04:
            return "Data Transfer Error";
        case 0x05:
            return "Aborted due to Power Loss";
        default:
            return "Unknown Generic Error";
        }
    }
    else if (sct == 0x1)
    { // Command Specific Status
        switch (sc)
        {
        case 0x80:
            return "LBA Out of Range";
        case 0x81:
            return "Capacity Exceeded";
        case 0x82:
            return "Namespace Not Ready";
        default:
            return "Unknown Command Specific Error";
        }
    }
    else
    {
        return "Unknown Status Code Type";
    }
}

#define MAX_RETRIES 3

// 복사 명령 보내는 함수
int send_copy_command(int fd, struct nvme_admin_cmd *cmd)
{
    int ret, retries = 0;

    do
    {
        ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, cmd);
        if (ret == 0)
        {
            uint16_t status = cmd->result;
            uint8_t sct = (status >> 8) & 0x7F;
            uint8_t sc = status & 0xFF;

            if (status == 0)
            {
                printf("FDP Copy Command Completed Successfully!\n");
                return 0; // 성공
            }
            else
            {
                printf("Command Failed: %s\n", decode_nvme_status(sct, sc));

                // 일부 오류에 대해서만 재시도
                if (sct == 0x0 && (sc == 0x04 || sc == 0x05))
                {
                    // Data Transfer Error, Power Loss 같은 경우
                    printf("Recoverable Error. Retrying...\n");
                }
                else
                {
                    printf("Fatal Error. Giving up.\n");
                    return -1;
                }
            }
        }
        else
        {
            perror("ioctl NVME_IOCTL_ADMIN_CMD");
            return -1;
        }
        retries++;
    } while (retries <= MAX_RETRIES);

    printf("Exceeded Maximum Retry Count\n");
    return -1;
}

int main()
{
    const char *dev_path = "/dev/nvme0"; // NVMe 관리 디바이스
    int fd;
    struct nvme_admin_cmd cmd;
    uint32_t nsid = 1; // Namespace ID
    struct copy_entry *entry_table;
    size_t table_size;
    void *dma_buffer;
    int i;

    // 1. Copy Entry 테이블 준비
    table_size = MAX_COPY_ENTRIES * sizeof(struct copy_entry);
    posix_memalign(&dma_buffer, 4096, table_size); // 4K aligned memory
    if (!dma_buffer)
    {
        perror("posix_memalign");
        return 1;
    }
    memset(dma_buffer, 0, table_size);

    entry_table = (struct copy_entry *)dma_buffer;

    // 2. 복사할 엔트리 채우기
    for (i = 0; i < MAX_COPY_ENTRIES; i++)
    {
        entry_table[i].slba = 1000 + (i * 100); // SLBA 1000, 1100, 1200, 1300
        entry_table[i].nlb = 7;                 // 8개 블록 복사 (0-based라서 7)
        entry_table[i].reserved = 0;
        entry_table[i].placement_id = 3 + i; // PID 3, 4, 5, 6 부여
    }

    // 3. 디바이스 열기
    fd = open(dev_path, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        free(dma_buffer);
        return 1;
    }

    // 4. Admin Command 설정
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_FDP_COPY_OPCODE;
    cmd.nsid = nsid;
    cmd.addr = (uintptr_t)dma_buffer; // PRP1 주소 설정
    cmd.data_len = table_size;        // 전송할 데이터 길이
    cmd.cdw10 = MAX_COPY_ENTRIES - 1; // Entry 개수 - 1 (0-based count)

    cmd.timeout_ms = 5000;
    send_copy_command(fd, &cmd);

    uint16_t status = cmd.result;
    uint8_t sct = (status >> 8) & 0x7F;
    uint8_t sc = status & 0xFF;

    printf("Decode Result: %s\n", decode_nvme_status(sct, sc));

    // 성공 여부 체크
    if (status == 0)
    {
        printf("FDP Copy Command Completed Successfully!\n");
    }
    else
    {
        printf("FDP Copy Command Failed. Decoding Status...\n");
    }

    // 6. 정리
    free(dma_buffer);
    close(fd);
    return 0;
}
