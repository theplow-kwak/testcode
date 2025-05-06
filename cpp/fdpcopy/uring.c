#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <liburing.h>
#include <linux/nvme_ioctl.h>

#define QUEUE_DEPTH 8
#define BUFFER_SIZE 4096

// NVMe 명령 구조체
// 요청 추적용 구조체
struct io_data {
    void *buf;
    int fd;
    struct nvme_passthru_cmd cmd;
    int submitted;
    int completed;
};

int main(int argc, char *argv[]) {
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct io_data *data;
    int ret, i, fd;
    char *device_path = "/dev/nvme0n1";  // 사용할 NVMe 장치 경로
    
    // NVMe 장치 열기
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open NVMe device");
        return 1;
    }
    
    // io_uring 초기화 (Big SQE 및 CQE 지원을 위한 플래그 추가)
    ret = io_uring_queue_init(QUEUE_DEPTH, &ring, IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
    if (ret < 0) {
        perror("io_uring_queue_init failed");
        close(fd);
        return 1;
    }
    
    // I/O 요청 데이터 준비
    data = calloc(QUEUE_DEPTH, sizeof(struct io_data));
    for (i = 0; i < QUEUE_DEPTH; i++) {
        data[i].fd = fd;
        data[i].buf = aligned_alloc(4096, BUFFER_SIZE);  // 페이지 정렬 버퍼
        if (!data[i].buf) {
            perror("Failed to allocate buffer");
            goto cleanup;
        }
        
        // NVMe 읽기 명령 설정 (예: 읽기 작업)
        data[i].cmd.opcode = 0x02;  // NVMe Read opcode
        data[i].cmd.nsid = 1;  // 네임스페이스 ID (장치에 맞게 수정)
        data[i].cmd.addr = (unsigned long)data[i].buf;
        data[i].cmd.data_len = BUFFER_SIZE;
        data[i].cmd.cdw10 = i;  // LBA 시작 주소 (섹터 단위, 예시)
        data[i].cmd.cdw11 = 0;
        data[i].cmd.cdw12 = (BUFFER_SIZE / 512) - 1;  // 섹터 수 - 1
        data[i].cmd.timeout_ms = 5000;  // 타임아웃 5초
    }
    
    printf("Submitting %d async I/O requests...\n", QUEUE_DEPTH);
    
    // 모든 I/O 요청 제출
    for (i = 0; i < QUEUE_DEPTH; i++) {
        sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            fprintf(stderr, "Could not get SQE\n");
            goto cleanup;
        }
        
        // NVMe passthrough 명령 준비
        io_uring_prep_uring_cmd(sqe, &data[i].cmd, sizeof(data[i].cmd), data[i].fd);
        io_uring_sqe_set_data(sqe, &data[i]);  // 나중에 매핑할 수 있도록 데이터 포인터 설정
        data[i].submitted = 1;
    }
    
    // 준비된 요청 모두 제출
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        perror("io_uring_submit failed");
        goto cleanup;
    }
    
    printf("Successfully submitted %d I/O requests\n", ret);
    
    // 완료 처리 (모든 요청이 완료될 때까지)
    for (i = 0; i < QUEUE_DEPTH; i++) {
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            perror("io_uring_wait_cqe failed");
            goto cleanup;
        }
        
        struct io_data *completed_data = io_uring_cqe_get_data(cqe);
        completed_data->completed = 1;
        
        if (cqe->res < 0) {
            fprintf(stderr, "I/O error: %s\n", strerror(-cqe->res));
        } else {
            printf("I/O completed successfully, result: %d\n", cqe->res);
            // 읽은 데이터 처리 (예: 첫 16바이트 출력)
            printf("Data sample: ");
            for (int j = 0; j < 16 && j < BUFFER_SIZE; j++) {
                printf("%02x ", ((unsigned char *)completed_data->buf)[j]);
            }
            printf("\n");
        }
        
        io_uring_cqe_seen(&ring, cqe);
    }

cleanup:
    // 자원 정리
    for (i = 0; i < QUEUE_DEPTH; i++) {
        if (data[i].buf) {
            free(data[i].buf);
        }
    }
    free(data);
    io_uring_queue_exit(&ring);
    close(fd);
    
    return 0;
}