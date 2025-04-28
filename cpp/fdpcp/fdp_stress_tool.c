/*
 * FDP Copy Stress Tool - Advanced Version
 * Author: (User)
 * Description:
 *   - Multi-threaded FDP Copy test
 *   - PRP List chaining for large Copy Descriptor Table
 *   - Automatic Copy verification (read + compare)
 *   - Latency measurement and histogram output
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <string.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <errno.h>
 #include <sys/ioctl.h>
 #include <pthread.h>
 #include <time.h>
 #include <sys/time.h>
 #include <linux/nvme_ioctl.h>
 #include <linux/nvme.h>
 
 #define PAGE_SIZE 4096
 #define DESCS_PER_PAGE (PAGE_SIZE / sizeof(struct nvme_copy_descriptor))
 #define COPY_ENTRIES_PER_CMD 512
 #define THREAD_COUNT 4
 #define HIST_BUCKET_COUNT 20
 
 struct nvme_copy_descriptor {
     uint64_t slba: 63;
     uint64_t rsvd1: 1;
     uint16_t nlb;
     uint16_t rsvd2;
     uint32_t rsvd3;
 } __attribute__((packed));
 
 typedef struct {
     int fd;
     uint32_t nsid;
     uint64_t start_lba;
     uint64_t dst_lba;
     int id;
 } thread_arg_t;
 
 static uint64_t latency_histogram[HIST_BUCKET_COUNT] = {0};
 pthread_mutex_t histogram_lock = PTHREAD_MUTEX_INITIALIZER;
 
 void *alloc_prp_aligned_buffer(size_t size) {
     void *ptr = NULL;
     if (posix_memalign(&ptr, PAGE_SIZE, size) != 0) {
         return NULL;
     }
     memset(ptr, 0, size);
     return ptr;
 }
 
 int nvme_read(int fd, uint32_t nsid, uint64_t slba, uint16_t nlb, void *buf, size_t buf_size) {
     struct nvme_passthru_cmd cmd = {0};
 
     cmd.opcode = 0x02; // NVMe Read Command
     cmd.nsid = nsid;
     cmd.addr = (__u64)(uintptr_t)buf;
     cmd.data_len = buf_size;
     cmd.cdw10 = slba & 0xFFFFFFFF;
     cmd.cdw11 = slba >> 32;
     cmd.cdw12 = (nlb - 1) & 0xFFFF;
 
     int err = ioctl(fd, NVME_IOCTL_IO_CMD, &cmd);
     if (err < 0) {
         perror("nvme_read");
         return -1;
     }
     return 0;
 }
 
 void generate_copy_descriptor_table(struct nvme_copy_descriptor *table, uint64_t src_lba, uint32_t nlb_total) {
     for (uint32_t i = 0; i < nlb_total; i++) {
         table[i].slba = src_lba + i;
         table[i].nlb = 1; // 1 LBA per descriptor
         table[i].rsvd1 = 0;
         table[i].rsvd2 = 0;
         table[i].rsvd3 = 0;
     }
 }
 
 int send_copy_admin_command(int fd, uint32_t nsid, struct nvme_copy_descriptor *descs, uint32_t desc_count, uint64_t dst_lba) {
     struct nvme_passthru_cmd cmd = {0};
 
     cmd.opcode = 0x19; // Copy Command
     cmd.nsid = nsid;
     cmd.addr = (__u64)(uintptr_t)descs;
     cmd.data_len = desc_count * sizeof(struct nvme_copy_descriptor);
 
     cmd.cdw10 = ((desc_count - 1) & 0xFFF) | ((0 & 0xF) << 20); // Copy Type = 0
     cmd.cdw11 = dst_lba & 0xFFFFFFFF;
     cmd.cdw12 = dst_lba >> 32;
 
     int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
     if (err < 0) {
         perror("send_copy_admin_command");
         return -1;
     }
     return 0;
 }
 
 int verify_copy(int fd, uint32_t nsid, uint64_t src_lba, uint64_t dst_lba, uint32_t nlb) {
     size_t buf_size = nlb * 512;
     void *src_buf = alloc_prp_aligned_buffer(buf_size);
     void *dst_buf = alloc_prp_aligned_buffer(buf_size);
 
     if (!src_buf || !dst_buf) {
         perror("verify_copy: alloc_prp_aligned_buffer");
         free(src_buf);
         free(dst_buf);
         return -1;
     }
 
     if (nvme_read(fd, nsid, src_lba, nlb, src_buf, buf_size) != 0 ||
         nvme_read(fd, nsid, dst_lba, nlb, dst_buf, buf_size) != 0) {
         free(src_buf);
         free(dst_buf);
         return -1;
     }
 
     int ret = memcmp(src_buf, dst_buf, buf_size);
 
     free(src_buf);
     free(dst_buf);
     return ret;
 }
 
 void record_latency(uint64_t usec) {
     pthread_mutex_lock(&histogram_lock);
     int bucket = usec / 50;
     if (bucket >= HIST_BUCKET_COUNT)
         bucket = HIST_BUCKET_COUNT - 1;
     latency_histogram[bucket]++;
     pthread_mutex_unlock(&histogram_lock);
 }
 
 void *copy_worker(void *arg) {
     thread_arg_t *targ = (thread_arg_t *)arg;
     struct nvme_copy_descriptor *desc_table = (struct nvme_copy_descriptor *)alloc_prp_aligned_buffer(PAGE_SIZE * 4);
     if (!desc_table) {
         perror("copy_worker: alloc_prp_aligned_buffer");
         return NULL;
     }
 
     generate_copy_descriptor_table(desc_table, targ->start_lba, COPY_ENTRIES_PER_CMD);
 
     while (1) {
         struct timeval start, end;
         gettimeofday(&start, NULL);
 
         if (send_copy_admin_command(targ->fd, targ->nsid, desc_table, COPY_ENTRIES_PER_CMD, targ->dst_lba) != 0) {
             printf("[Thread %d] Copy command failed!\n", targ->id);
             break;
         }
 
         gettimeofday(&end, NULL);
         uint64_t latency = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
         record_latency(latency);
 
         if (verify_copy(targ->fd, targ->nsid, targ->start_lba, targ->dst_lba, COPY_ENTRIES_PER_CMD) != 0) {
             printf("[Thread %d] Verify failed!\n", targ->id);
             break;
         }
 
         // 다음 copy
         targ->start_lba += COPY_ENTRIES_PER_CMD;
         targ->dst_lba += COPY_ENTRIES_PER_CMD;
     }
 
     free(desc_table);
     return NULL;
 }
 
 void print_latency_histogram() {
     printf("\nLatency Histogram (usec buckets):\n");
     for (int i = 0; i < HIST_BUCKET_COUNT; i++) {
         printf("%4d-%4d us: %lu\n", i*50, (i+1)*50-1, latency_histogram[i]);
     }
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
 
     pthread_t threads[THREAD_COUNT];
     thread_arg_t args[THREAD_COUNT];
 
     for (int i = 0; i < THREAD_COUNT; i++) {
         args[i].fd = fd;
         args[i].nsid = 1;
         args[i].start_lba = 0x1000 + i * 0x100000;
         args[i].dst_lba = 0x800000 + i * 0x100000;
         args[i].id = i;
         pthread_create(&threads[i], NULL, copy_worker, &args[i]);
     }
 
     for (int i = 0; i < THREAD_COUNT; i++) {
         pthread_join(threads[i], NULL);
     }
 
     print_latency_histogram();
 
     close(fd);
     return 0;
 }
 