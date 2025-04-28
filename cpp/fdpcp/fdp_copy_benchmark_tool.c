/*
 * FDP Copy Benchmark Tool
 * Author: (User)
 * Description:
 *   - Massive FDP Copy test
 *   - Measures Throughput (MB/s) and IOPS
 *   - Collects latency statistics
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <string.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <errno.h>
 #include <sys/ioctl.h>
 #include <sys/time.h>
 #include <linux/nvme_ioctl.h>
 #include <linux/nvme.h>
 
 #define PAGE_SIZE 4096
 #define COPY_BATCH_SIZE 512  // 엔트리 수
 #define TOTAL_COPY_BATCHES 2000 // 총 반복 횟수
 #define TOTAL_COPY_ENTRIES (COPY_BATCH_SIZE * TOTAL_COPY_BATCHES)
 #define LBA_SIZE 512  // 보통 512 bytes
 #define COPY_CMD_OPCODE 0x19
 
 struct nvme_copy_descriptor {
     uint64_t slba: 63;
     uint64_t rsvd1: 1;
     uint16_t nlb;
     uint16_t rsvd2;
     uint32_t rsvd3;
 } __attribute__((packed));
 
 void *alloc_prp_aligned_buffer(size_t size) {
     void *ptr = NULL;
     if (posix_memalign(&ptr, PAGE_SIZE, size) != 0) {
         return NULL;
     }
     memset(ptr, 0, size);
     return ptr;
 }
 
 void generate_copy_descriptors(struct nvme_copy_descriptor *table, uint64_t src_start_lba) {
     for (int i = 0; i < COPY_BATCH_SIZE; i++) {
         table[i].slba = src_start_lba + i;
         table[i].nlb = 1;
         table[i].rsvd1 = 0;
         table[i].rsvd2 = 0;
         table[i].rsvd3 = 0;
     }
 }
 
 int send_copy_command(int fd, uint32_t nsid, struct nvme_copy_descriptor *table, uint64_t dst_lba) {
     struct nvme_passthru_cmd cmd = {0};
 
     cmd.opcode = COPY_CMD_OPCODE;
     cmd.nsid = nsid;
     cmd.addr = (__u64)(uintptr_t)table;
     cmd.data_len = COPY_BATCH_SIZE * sizeof(struct nvme_copy_descriptor);
     cmd.cdw10 = ((COPY_BATCH_SIZE - 1) & 0xFFF) | (0 << 20); // Copy Type 0
     cmd.cdw11 = dst_lba & 0xFFFFFFFF;
     cmd.cdw12 = dst_lba >> 32;
 
     int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
     if (err < 0) {
         perror("send_copy_command");
         return -1;
     }
     return 0;
 }
 
 double time_diff_sec(struct timeval *start, struct timeval *end) {
     double sec = (end->tv_sec - start->tv_sec);
     double usec = (end->tv_usec - start->tv_usec) / 1000000.0;
     return sec + usec;
 }
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         printf("Usage: %s /dev/nvme0\n", argv[0]);
         return -1;
     }
 
     const char *dev_path = argv[1];
     int fd = open(dev_path, O_RDWR);
     if (fd < 0) {
         perror("open");
         return -1;
     }
 
     uint32_t nsid = 1;
     uint64_t src_lba = 0x10000;
     uint64_t dst_lba = 0x80000;
 
     struct nvme_copy_descriptor *desc_table = (struct nvme_copy_descriptor *)alloc_prp_aligned_buffer(PAGE_SIZE * 4);
     if (!desc_table) {
         perror("alloc_prp_aligned_buffer");
         close(fd);
         return -1;
     }
 
     printf("Starting FDP Copy Benchmark...\n");
     struct timeval total_start, total_end;
     gettimeofday(&total_start, NULL);
 
     uint64_t total_latency_usec = 0;
     int success_count = 0;
 
     for (int i = 0; i < TOTAL_COPY_BATCHES; i++) {
         generate_copy_descriptors(desc_table, src_lba + (i * COPY_BATCH_SIZE));
 
         struct timeval start, end;
         gettimeofday(&start, NULL);
 
         if (send_copy_command(fd, nsid, desc_table, dst_lba + (i * COPY_BATCH_SIZE)) == 0) {
             success_count++;
         } else {
             printf("[Batch %d] Copy command failed.\n", i);
         }
 
         gettimeofday(&end, NULL);
 
         total_latency_usec += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
     }
 
     gettimeofday(&total_end, NULL);
 
     double total_time_sec = time_diff_sec(&total_start, &total_end);
     double avg_latency_usec = total_latency_usec / (double)success_count;
     double throughput_mb_s = (TOTAL_COPY_ENTRIES * LBA_SIZE) / (1024.0 * 1024.0) / total_time_sec;
     double iops = TOTAL_COPY_ENTRIES / total_time_sec;
 
     printf("\n=== Benchmark Result ===\n");
     printf("Total Time     : %.3f sec\n", total_time_sec);
     printf("Total Entries  : %d\n", TOTAL_COPY_ENTRIES);
     printf("Success Count  : %d\n", success_count);
     printf("Average Latency: %.2f usec\n", avg_latency_usec);
     printf("Throughput     : %.2f MB/s\n", throughput_mb_s);
     printf("IOPS           : %.2f\n", iops);
 
     free(desc_table);
     close(fd);
     return 0;
 }
 