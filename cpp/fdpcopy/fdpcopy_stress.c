/*
 * FDP Copy Stress Tool - Full Feature Version
 * Author: (User)
 * Description: Advanced NVMe FDP Copy stress test tool with PRP List, error handling,
 * auto verification, latency histogram, multi-threading support.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
// #include <linux/nvme.h>

#define PAGE_SIZE 4096
#define MAX_COPY_DESC 128
#define THREAD_COUNT 4
#define HISTO_BUCKETS 20

struct nvme_copy_descriptor
{
    uint64_t slba : 63;
    uint64_t rsvd1 : 1;
    uint16_t nlb;
    uint16_t rsvd2;
    uint32_t rsvd3;
} __attribute__((packed));

uint64_t latency_histogram[HISTO_BUCKETS] = {0};

uint64_t get_usec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000000ULL) + (ts.tv_nsec / 1000);
}

void update_latency_histogram(uint64_t latency)
{
    int idx = latency / 100;
    if (idx >= HISTO_BUCKETS)
        idx = HISTO_BUCKETS - 1;
    latency_histogram[idx]++;
}

void print_latency_histogram()
{
    printf("\nLatency Histogram (us):\n");
    for (int i = 0; i < HISTO_BUCKETS; i++)
    {
        printf("%4d-%4d us: %lu\n", i * 100, (i + 1) * 100, latency_histogram[i]);
    }
}

int nvme_read(int fd, __u32 nsid, __u64 slba, __u16 nlb, void *buffer)
{
    struct nvme_user_io io = {
        .opcode = 0x02,
        .nblocks = nlb - 1,
        .addr = (__u64)(uintptr_t)buffer,
        .slba = slba,
        // .nsid = nsid,
    };

    int err = ioctl(fd, NVME_IOCTL_SUBMIT_IO, &io);
    if (err < 0)
    {
        perror("nvme_read ioctl");
        return -1;
    }
    return 0;
}

int verify_copy(int fd, uint64_t src_lba, uint64_t dst_lba, uint32_t nlb)
{
    void *src_buf = aligned_alloc(PAGE_SIZE, nlb * 512);
    void *dst_buf = aligned_alloc(PAGE_SIZE, nlb * 512);
    if (!src_buf || !dst_buf)
    {
        perror("aligned_alloc");
        return -1;
    }

    if (nvme_read(fd, 1, src_lba, nlb, src_buf))
    {
        free(src_buf);
        free(dst_buf);
        return -1;
    }
    if (nvme_read(fd, 1, dst_lba, nlb, dst_buf))
    {
        free(src_buf);
        free(dst_buf);
        return -1;
    }

    int result = memcmp(src_buf, dst_buf, nlb * 512);
    free(src_buf);
    free(dst_buf);
    return result;
}

void generate_copy_descriptor_table(void *buf, int desc_count, uint64_t max_lba)
{
    struct nvme_copy_descriptor *desc = (struct nvme_copy_descriptor *)buf;
    for (int i = 0; i < desc_count; i++)
    {
        desc[i].slba = rand() % (max_lba - 1000);
        desc[i].nlb = (rand() % 8) + 1;
        desc[i].rsvd1 = 0;
        desc[i].rsvd2 = 0;
        desc[i].rsvd3 = 0;
    }
}

int send_copy_admin_command(int fd, void *cdt, int desc_count, uint64_t dst_lba)
{
    struct nvme_passthru_cmd cmd = {0};
    cmd.opcode = 0x19;
    cmd.nsid = 1;
    cmd.addr = (__u64)(uintptr_t)cdt;
    cmd.data_len = desc_count * sizeof(struct nvme_copy_descriptor);
    cmd.cdw10 = ((desc_count - 1) & 0xFFF) | ((0 & 0xF) << 20);
    cmd.cdw11 = dst_lba & 0xFFFFFFFF;
    cmd.cdw12 = dst_lba >> 32;
    cmd.timeout_ms = 0;

    return ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

typedef struct
{
    int fd;
    uint64_t max_lba;
    int iterations;
    int thread_id;
} thread_arg_t;

void *copy_worker_thread(void *arg)
{
    thread_arg_t *targ = (thread_arg_t *)arg;

    void *cdt_buffer = aligned_alloc(PAGE_SIZE, MAX_COPY_DESC * sizeof(struct nvme_copy_descriptor));
    if (!cdt_buffer)
    {
        perror("aligned_alloc");
        pthread_exit(NULL);
    }

    for (int i = 0; i < targ->iterations; i++)
    {
        int desc_count = (rand() % MAX_COPY_DESC) + 1;
        uint64_t dst_lba = rand() % (targ->max_lba - 1000);

        generate_copy_descriptor_table(cdt_buffer, desc_count, targ->max_lba);

        uint64_t start = get_usec();
        int ret = send_copy_admin_command(targ->fd, cdt_buffer, desc_count, dst_lba);
        uint64_t end = get_usec();
        uint64_t latency = end - start;

        update_latency_histogram(latency);

        if (ret == 0)
        {
            if (verify_copy(targ->fd, ((struct nvme_copy_descriptor *)cdt_buffer)->slba, dst_lba, ((struct nvme_copy_descriptor *)cdt_buffer)->nlb) != 0)
            {
                printf("[T%d] Data mismatch!\n", targ->thread_id);
            }
        }
        else
        {
            printf("[T%d] Copy command failed!\n", targ->thread_id);
        }
    }

    free(cdt_buffer);
    pthread_exit(NULL);
}

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
    else if (sct == 0x7)
    { // Vendor Specific Status
        return decode_vendor_status(sc);
    }
    else
    {
        return "Unknown Status Code Type";
    }
}

int main(int argc, char *argv[])
{
    srand(time(NULL));

    int fd = open("/dev/nvme0n1", O_RDWR);
    if (fd < 0)
    {
        perror("open nvme device");
        return -1;
    }

    uint64_t max_lba = 0x1000000; // 16M LBA 예시

    pthread_t threads[THREAD_COUNT];
    thread_arg_t args[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; i++)
    {
        args[i].fd = fd;
        args[i].max_lba = max_lba;
        args[i].iterations = 1000;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, copy_worker_thread, &args[i]);
    }

    for (int i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], NULL);
    }

    print_latency_histogram();

    close(fd);
    return 0;
}
