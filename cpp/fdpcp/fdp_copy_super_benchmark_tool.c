/*
 * FDP Copy Super Benchmark Tool
 * Author: (User)
 * Description:
 *   - Multi-threaded FDP Copy benchmark
 *   - Measures Throughput, IOPS, Latency
 *   - Supports custom src/dst LBA
 *   - Saves result to CSV if requested
 */

 #define _GNU_SOURCE
 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <string.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <errno.h>
 #include <sys/ioctl.h>
 #include <sys/time.h>
 #include <linux/nvme_ioctl.h>
 #include <linux/nvme.h>
 #include <time.h>
 #include <getopt.h>
 
 #define PAGE_SIZE 4096
 #define MAX_RETRIES 3
 #define COPY_CMD_OPCODE 0x19
 
 typedef struct {
     int thread_id;
     int fd;
     uint32_t nsid;
     uint64_t src_lba;
     uint64_t dst_lba;
     int batch_size;
     int batches;
     uint64_t total_entries;
     uint64_t success_count;
     uint64_t retry_count;
     uint64_t fail_count;
     double total_latency_usec;
 } thread_arg_t;
 
 void *alloc_prp_aligned_buffer(size_t size) {
     void *ptr = NULL;
     if (posix_memalign(&ptr, PAGE_SIZE, size) != 0) {
         return NULL;
     }
     memset(ptr, 0, size);
     return ptr;
 }
 
 void generate_copy_descriptors(struct nvme_copy_descriptor *table, uint64_t src_start_lba, int batch_size) {
     for (int i = 0; i < batch_size; i++) {
         table[i].slba = src_start_lba + i;
         table[i].nlb = 1;
         table[i].rsvd1 = 0;
         table[i].rsvd2 = 0;
         table[i].rsvd3 = 0;
     }
 }
 
 int send_copy_command(int fd, uint32_t nsid, struct nvme_copy_descriptor *table, int batch_size, uint64_t dst_lba) {
     struct nvme_passthru_cmd cmd = {0};
 
     cmd.opcode = COPY_CMD_OPCODE;
     cmd.nsid = nsid;
     cmd.addr = (__u64)(uintptr_t)table;
     cmd.data_len = batch_size * sizeof(struct nvme_copy_descriptor);
     cmd.cdw10 = ((batch_size - 1) & 0xFFF) | (0 << 20); // Copy Type 0
     cmd.cdw11 = dst_lba & 0xFFFFFFFF;
     cmd.cdw12 = dst_lba >> 32;
 
     int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
     if (err < 0) {
         return -1;
     }
     return 0;
 }
 
 double time_diff_usec(struct timespec *start, struct timespec *end) {
     double sec = end->tv_sec - start->tv_sec;
     double nsec = (end->tv_nsec - start->tv_nsec) / 1000.0;
     return sec * 1000000.0 + nsec;
 }
 
 void *copy_worker(void *arg) {
     thread_arg_t *targ = (thread_arg_t *)arg;
 
     struct nvme_copy_descriptor *desc_table = (struct nvme_copy_descriptor *)alloc_prp_aligned_buffer(PAGE_SIZE * 4);
     if (!desc_table) {
         perror("alloc_prp_aligned_buffer");
         pthread_exit(NULL);
     }
 
     for (int i = 0; i < targ->batches; i++) {
         uint64_t src = targ->src_lba + i * targ->batch_size;
         uint64_t dst = targ->dst_lba + i * targ->batch_size;
 
         generate_copy_descriptors(desc_table, src, targ->batch_size);
 
         struct timespec start, end;
         clock_gettime(CLOCK_MONOTONIC, &start);
 
         int attempt = 0;
         while (attempt <= MAX_RETRIES) {
             if (send_copy_command(targ->fd, targ->nsid, desc_table, targ->batch_size, dst) == 0) {
                 clock_gettime(CLOCK_MONOTONIC, &end);
                 double latency = time_diff_usec(&start, &end);
                 targ->total_latency_usec += latency;
                 targ->success_count++;
                 break;
             } else {
                 attempt++;
                 targ->retry_count++;
                 if (attempt > MAX_RETRIES) {
                     targ->fail_count++;
                     break;
                 }
             }
         }
     }
 
     free(desc_table);
     pthread_exit(NULL);
 }
 
 void print_usage(const char *prog) {
     printf("Usage: %s [options] <device>\n", prog);
     printf("Options:\n");
     printf("  --threads=N        Number of threads (default 4)\n");
     printf("  --batch-size=N     Copy entries per command (default 512)\n");
     printf("  --batches=N        Number of batches per thread (default 1000)\n");
     printf("  --namespace-id=N   Namespace ID (default 1)\n");
     printf("  --src-lba=N        Source start LBA (default 0x10000)\n");
     printf("  --dst-lba=N        Destination start LBA (default 0x80000)\n");
     printf("  --csv              Save results to result.csv\n");
 }
 
 int main(int argc, char *argv[]) {
     int threads = 4;
     int batch_size = 512;
     int batches = 1000;
     uint32_t nsid = 1;
     uint64_t src_lba = 0x10000;
     uint64_t dst_lba = 0x80000;
     int save_csv = 0;
     const char *dev_path = NULL;
 
     static struct option long_options[] = {
         {"threads", required_argument, 0, 't'},
         {"batch-size", required_argument, 0, 'b'},
         {"batches", required_argument, 0, 'n'},
         {"namespace-id", required_argument, 0, 'i'},
         {"src-lba", required_argument, 0, 's'},
         {"dst-lba", required_argument, 0, 'd'},
         {"csv", no_argument, &save_csv, 1},
         {0, 0, 0, 0}
     };
 
     int opt;
     while ((opt = getopt_long(argc, argv, "t:b:n:i:s:d:", long_options, NULL)) != -1) {
         switch (opt) {
             case 't': threads = atoi(optarg); break;
             case 'b': batch_size = atoi(optarg); break;
             case 'n': batches = atoi(optarg); break;
             case 'i': nsid = atoi(optarg); break;
             case 's': src_lba = strtoull(optarg, NULL, 0); break;
             case 'd': dst_lba = strtoull(optarg, NULL, 0); break;
             case 0: break;
             default: print_usage(argv[0]); return 1;
         }
     }
 
     if (optind >= argc) {
         print_usage(argv[0]);
         return 1;
     }
     dev_path = argv[optind];
 
     int fd = open(dev_path, O_RDWR);
     if (fd < 0) {
         perror("open");
         return 1;
     }
 
     pthread_t *thread_ids = calloc(threads, sizeof(pthread_t));
     thread_arg_t *thread_args = calloc(threads, sizeof(thread_arg_t));
 
     struct timespec total_start, total_end;
     clock_gettime(CLOCK_MONOTONIC, &total_start);
 
     for (int i = 0; i < threads; i++) {
         thread_args[i].thread_id = i;
         thread_args[i].fd = fd;
         thread_args[i].nsid = nsid;
         thread_args[i].src_lba = src_lba + i * batch_size * batches;
         thread_args[i].dst_lba = dst_lba + i * batch_size * batches;
         thread_args[i].batch_size = batch_size;
         thread_args[i].batches = batches;
 
         pthread_create(&thread_ids[i], NULL, copy_worker, &thread_args[i]);
     }
 
     for (int i = 0; i < threads; i++) {
         pthread_join(thread_ids[i], NULL);
     }
 
     clock_gettime(CLOCK_MONOTONIC, &total_end);
 
     uint64_t total_entries = 0, total_success = 0, total_fail = 0, total_retries = 0;
     double total_latency = 0.0;
     for (int i = 0; i < threads; i++) {
         total_entries += (uint64_t)batch_size * batches;
         total_success += thread_args[i].success_count;
         total_fail += thread_args[i].fail_count;
         total_retries += thread_args[i].retry_count;
         total_latency += thread_args[i].total_latency_usec;
     }
 
     double total_sec = time_diff_usec(&total_start, &total_end) / 1000000.0;
     double throughput_mb_s = (total_entries * 512) / (1024.0 * 1024.0) / total_sec;
     double iops = total_entries / total_sec;
     double avg_latency = total_latency / total_success;
 
     printf("\n=== Super Benchmark Result ===\n");
     printf("Total Time      : %.3f sec\n", total_sec);
     printf("Total Entries   : %lu\n", total_entries);
     printf("Success         : %lu\n", total_success);
     printf("Retries         : %lu\n", total_retries);
     printf("Failures        : %lu\n", total_fail);
     printf("Throughput      : %.2f MB/s\n", throughput_mb_s);
     printf("IOPS            : %.2f\n", iops);
     printf("Avg Latency     : %.2f usec\n", avg_latency);
 
     if (save_csv) {
         FILE *fp = fopen("result.csv", "w");
         if (fp) {
             fprintf(fp, "TotalTime,TotalEntries,Success,Retries,Failures,ThroughputMBps,IOPS,AvgLatencyUsec\n");
             fprintf(fp, "%.3f,%lu,%lu,%lu,%lu,%.2f,%.2f,%.2f\n",
                 total_sec, total_entries, total_success, total_retries, total_fail,
                 throughput_mb_s, iops, avg_latency);
             fclose(fp);
             printf("Saved result to result.csv\n");
         }
     }
 
     free(thread_ids);
     free(thread_args);
     close(fd);
     return 0;
 }
 