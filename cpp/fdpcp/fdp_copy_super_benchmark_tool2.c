#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nvme.h>

#define MAX_COPY_ENTRIES 1024

// NVMe Copy 명령어용 구조체
struct nvme_passthru_cmd {
    uint8_t op_code;     // 0x86 = admin command
    uint8_t flags;
    uint8_t rsvd[2];
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t addr;
    uint32_t data_len;
    uint32_t control;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

// 복사 요청 구조체
typedef struct {
    int in_use;
    uint64_t src_lba;
    uint64_t dst_lba;
    struct nvme_passthru_cmd cmd;
} copy_request_t;

// 전역 변수 선언
int qdepth = 1;                  // 큐 깊이
int random_mode = 0;             // 랜덤 모드 플래그
uint64_t src_lba_base = 0;       // 시작 LBA (Source)
uint64_t dst_lba_base = 0;       // 시작 LBA (Destination)
uint64_t total_copy_ops = 0;     // 총 복사 작업 수
uint64_t completed_copy_ops = 0; // 완료된 복사 작업 수
struct timeval start_time;       // 시작 시간
copy_request_t reqs[MAX_COPY_ENTRIES]; // 복사 요청 배열

// 랜덤 LBA 계산 함수
uint64_t get_random_lba(uint64_t base, uint64_t range) {
    return base + (rand() % range);
}

// NVMe Status 코드 디코딩 함수
const char* decode_nvme_status(uint16_t status) {
    uint8_t sct = (status >> 8) & 0x07; // Status Code Type
    uint8_t sc = status & 0xFF;         // Status Code

    if (sct == 0x0) { // Generic Command Status
        switch (sc) {
            case 0x00: return "Success";
            case 0x01: return "Invalid Command Opcode";
            case 0x02: return "Invalid Field in Command";
            case 0x03: return "Command ID Conflict";
            case 0x04: return "Data Transfer Error";
            case 0x05: return "Aborted Power Loss";
            case 0x06: return "Internal Device Error";
            case 0x07: return "Aborted by Request";
            case 0x08: return "Aborted SQ Deletion";
            case 0x09: return "Aborted Failed Fused";
            case 0x0A: return "Aborted Missing Fused";
            case 0x0B: return "Invalid Namespace or Format";
            case 0x0C: return "Command Sequence Error";
            case 0x0D: return "Invalid SGL Segment Descriptor";
            default: return "Unknown Generic Command Error";
        }
    } else if (sct == 0x1) { // Command Specific Status
        switch (sc) {
            case 0x00: return "Completion Queue Invalid";
            case 0x01: return "Invalid Queue Identifier";
            case 0x02: return "Invalid Queue Size";
            case 0x03: return "Abort Command Limit Exceeded";
            case 0x04: return "Asynchronous Event Request Limit Exceeded";
            default: return "Unknown Command Specific Error";
        }
    } else if (sct == 0x2) { // Media and Data Integrity Errors
        switch (sc) {
            case 0x00: return "Write Fault";
            case 0x01: return "Unrecovered Read Error";
            case 0x02: return "End-to-End Guard Check Error";
            case 0x03: return "End-to-End Application Tag Check Error";
            case 0x04: return "End-to-End Reference Tag Check Error";
            default: return "Unknown Media/Integrity Error";
        }
    } else {
        return "Unknown Status Code Type";
    }
}

// Progress bar 출력 스레드
void* progress_thread(void* arg) {
    while (completed_copy_ops < total_copy_ops) {
        usleep(1000 * 1000); // 1초 대기

        struct timeval now;
        gettimeofday(&now, NULL);

        double elapsed = (now.tv_sec - start_time.tv_sec) + (now.tv_usec - start_time.tv_usec) / 1e6;
        double percent = 100.0 * completed_copy_ops / total_copy_ops;
        double speed = completed_copy_ops / elapsed;
        double remaining = (total_copy_ops - completed_copy_ops) / speed;

        printf("\r[");
        int bars = (int)(percent / 5); // 5%당 한 칸
        for (int i = 0; i < 20; i++) {
            if (i < bars) printf("#");
            else printf(".");
        }
        printf("] %.1f%% (ETA %02d:%02d) ", percent, (int)(remaining/60), (int)((int)remaining%60));
        fflush(stdout);
    }
    printf("\nCopy complete!\n");
    return NULL;
}

// Copy 명령어 준비 함수
void prepare_copy_command(copy_request_t *req, uint64_t src_lba, uint64_t dst_lba) {
    memset(&req->cmd, 0, sizeof(req->cmd));
    req->cmd.op_code = 0x86;  // Admin Command
    req->cmd.src_lba = src_lba;
    req->cmd.dst_lba = dst_lba;
    req->cmd.data_len = 4096;  // Example: 4KB data size
    req->cmd.control = 0; // Control value for command

    req->in_use = 1;
}

// Copy 명령어 전송
void submit_nvme_passthru(int fd, copy_request_t *req) {
    int result = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &req->cmd);
    if (result != 0) {
        uint16_t status = req->cmd.cdw0 & 0xFFFF;
        printf("Copy failed: %s (status=0x%04x)\n", decode_nvme_status(status), status);
    }
}

// Main 함수: 벤치마크 수행
int main(int argc, char *argv[]) {
    int fd = open("/dev/nvme0", O_RDWR); // NVMe device 열기
    if (fd == -1) {
        perror("Failed to open NVMe device");
        return 1;
    }

    // Argument 파싱
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--qdepth") == 0 && i + 1 < argc) {
            qdepth = atoi(argv[++i]);
        }
        if (strcmp(argv[i], "--random") == 0) {
            random_mode = 1;
        }
        if (strcmp(argv[i], "--src-lba") == 0 && i + 1 < argc) {
            src_lba_base = atoi(argv[++i]);
        }
        if (strcmp(argv[i], "--dst-lba") == 0 && i + 1 < argc) {
            dst_lba_base = atoi(argv[++i]);
        }
    }

    srand(time(NULL)); // 랜덤 Seed 초기화
    total_copy_ops = 10000;  // 예시로 10000번 복사 작업

    // Progress 스레드 시작
    pthread_t prog_thread;
    pthread_create(&prog_thread, NULL, progress_thread, NULL);

    // 복사 작업 수행
    for (uint64_t i = 0; i < total_copy_ops; i++) {
        uint64_t src_lba = random_mode ? get_random_lba(src_lba_base, 10000) : src_lba_base + i;
        uint64_t dst_lba = random_mode ? get_random_lba(dst_lba_base, 10000) : dst_lba_base + i;

        for (int j = 0; j < qdepth; j++) {
            if (!reqs[j].in_use) {
                prepare_copy_command(&reqs[j], src_lba, dst_lba);
                submit_nvme_passthru(fd, &reqs[j]);
                completed_copy_ops++;
            }
        }
    }

    // Progress 종료 대기
    pthread_join(prog_thread, NULL);

    close(fd); // NVMe device 닫기
    return 0;
}
