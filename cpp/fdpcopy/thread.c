#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libaio.h>
#include <errno.h>
#include <linux/nvme_ioctl.h>
#include <pthread.h>

#define MAX_EVENTS 32
#define BUFFER_SIZE 4096 * 128

// ioctl 명령 처리를 위한 워커 쓰레드 구조체
struct worker_data {
    int fd;
    struct nvme_passthru_cmd cmd;
    void *buffer;
    int complete;
    int result;
    unsigned int lba_offset; // 추가: 현재 작업의 LBA 오프셋
    unsigned int nblocks;    // 추가: 현재 작업의 블록 수
};

// ioctl을 실행할 워커 쓰레드 함수
void *nvme_ioctl_worker(void *arg) {
    struct worker_data *data = (struct worker_data *)arg;
    
    // NVMe 명령 실행 (ioctl은 블로킹 호출이지만 별도 쓰레드로 비동기 효과)
    data->result = ioctl(data->fd, NVME_IOCTL_IO_CMD, &data->cmd);
    data->complete = 1;
    
    return NULL;
}

int main(int argc, char* argv[]) {
    int fd;
    pthread_t workers[MAX_EVENTS];
    struct worker_data worker_data[MAX_EVENTS];

    // 인자 파싱: qdepth, total_size
    unsigned int chunk_size = BUFFER_SIZE; // 8섹터(4KB*8=32KB)씩 전송
    unsigned int qdepth = MAX_EVENTS;
    unsigned long long total_size = 4096ULL * 8 * 100; // 예시: 100개 chunk(약 3.2MB)
    if (argc >= 3) {
        qdepth = atoi(argv[1]);
        total_size = strtoull(argv[2], NULL, 10);
        if (qdepth == 0 || total_size == 0) {
            printf("Usage: %s <qdepth> <total_size_bytes>\n", argv[0]);
            return 1;
        }
        if (qdepth > MAX_EVENTS) {
            printf("qdepth는 최대 %d까지 지원합니다.\n", MAX_EVENTS);
            return 1;
        }
    } else {
        printf("Usage: %s <qdepth> <total_size_bytes>\n", argv[0]);
        printf("기본값: qdepth=%u, total_size=%llu\n", qdepth, total_size);
    }
    unsigned long long sent_size = 0;
    unsigned int lba_size = 4096 / 512 * 8; // 8섹터(4KB*8), 512B 섹터 가정
    unsigned int lba = 0;

    // NVMe 장치 열기
    fd = open("/dev/nvme0n1", O_RDWR);
    if (fd < 0) {
        perror("장치 열기 실패");
        return 1;
    }

    // 워커 버퍼 및 구조체 초기화
    for (int i = 0; i < qdepth; i++) {
        void *buf;
        if (posix_memalign(&buf, 4096, chunk_size)) {
            perror("메모리 할당 실패");
            close(fd);
            return 1;
        }
        memset(buf, 0, chunk_size);
        worker_data[i].fd = fd;
        worker_data[i].buffer = buf;
        worker_data[i].complete = 0;
    }

    printf("qdepth=%d, chunk_size=%u, total_size=%llu\n", qdepth, chunk_size, total_size);

    int active_threads = 0;
    int finished = 0;
    while (sent_size < total_size || active_threads > 0) {
        // 새 작업 할당
        for (int i = 0; i < qdepth && sent_size < total_size; i++) {
            if (worker_data[i].complete == 0 && worker_data[i].result == 0) {
                // 아직 미할당 상태면 새 작업 할당
                unsigned int this_chunk = (total_size - sent_size) > chunk_size ? chunk_size : (total_size - sent_size);
                unsigned int nblocks = this_chunk / 512; // 512B 섹터 단위
                memset(&worker_data[i].cmd, 0, sizeof(struct nvme_passthru_cmd));
                worker_data[i].cmd.opcode = 0x02; // NVMe Read (0x02)
                worker_data[i].cmd.nsid = 1;
                worker_data[i].cmd.addr = (unsigned long)worker_data[i].buffer;
                worker_data[i].cmd.data_len = this_chunk;
                worker_data[i].cmd.cdw10 = lba; // LBA
                worker_data[i].cmd.cdw12 = nblocks - 1; // nblocks-1
                worker_data[i].cmd.timeout_ms = 5000;
                worker_data[i].lba_offset = lba;
                worker_data[i].nblocks = nblocks;
                worker_data[i].complete = 0;
                worker_data[i].result = 0;
                if (pthread_create(&workers[i], NULL, nvme_ioctl_worker, &worker_data[i])) {
                    perror("쓰레드 생성 실패");
                    free(worker_data[i].buffer);
                    close(fd);
                    return 1;
                }
                sent_size += this_chunk;
                lba += nblocks;
                active_threads++;
            }
        }
        // 완료된 쓰레드 join 및 결과 처리
        for (int i = 0; i < qdepth; i++) {
            if (worker_data[i].complete == 1) {
                pthread_join(workers[i], NULL);
                if (worker_data[i].result < 0) {
                    printf("청크(lba=%u) 실패: %d\n", worker_data[i].lba_offset, worker_data[i].result);
                } else {
                    printf("청크(lba=%u) 성공, 결과: %d\n", worker_data[i].lba_offset, worker_data[i].cmd.result);
                    printf("  데이터 샘플: ");
                    for (int j = 0; j < 16 && j < chunk_size; j++) {
                        printf("%02x ", ((unsigned char *)worker_data[i].buffer)[j]);
                    }
                    printf("\n");
                }
                worker_data[i].complete = 0;
                worker_data[i].result = 0;
                active_threads--;
                finished++;
            }
        }
    }
    // 버퍼 해제
    for (int i = 0; i < qdepth; i++) {
        free(worker_data[i].buffer);
    }
    close(fd);
    printf("총 %d개 청크 전송 완료\n", finished);
    return 0;
}