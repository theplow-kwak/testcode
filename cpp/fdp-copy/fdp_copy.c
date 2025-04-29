#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <getopt.h>
#include <libnvme.h>

#define MAX_RANGES 32

typedef struct {
    int thread_id;
    int fd;
    int eid;
    int qdepth;
    int random;
    int nsid;
    uint64_t src_lba;
    uint64_t dst_lba;
    int range_count;
    int total_iters;
    int quiet;
    int errors;
    int copied;
    double elapsed;
} thread_args_t;

uint64_t rand_lba(uint64_t base, uint64_t range) {
    return base + (rand() % range);
}

void* copy_thread_func(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;
    struct timeval start, end;
    struct nvme_copy_range ranges[MAX_RANGES];
    struct nvme_copy_args copy_args = {0};

    gettimeofday(&start, NULL);
    int copy_success = 0, copy_fail = 0;

    for (int i = 0; i < args->total_iters; i++) {
        for (int j = 0; j < args->range_count; j++) {
            ranges[j].slba = args->random ? rand_lba(args->src_lba, 1024) : (args->src_lba + j);
            ranges[j].nlb = 0;
            ranges[j].eid = args->eid;
        }

        copy_args.sdlba = args->dst_lba;
        copy_args.copy = ranges;
        copy_args.nr = args->range_count;
        copy_args.nsid = args->nsid;
        copy_args.fd = args->fd;
        copy_args.args_size = sizeof(copy_args);

        int ret = nvme_copy(&copy_args);
        if (ret < 0) {
            copy_fail++;
        } else {
            copy_success++;
        }
    }

    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;

    args->errors = copy_fail;
    args->copied = copy_success;
    args->elapsed = elapsed;

    if (!args->quiet) {
        printf("[Thread %d] Success: %d, Fail: %d, Time: %.3f sec, Throughput: %.2f MB/s\n",
               args->thread_id, copy_success, copy_fail, elapsed,
               (double)(copy_success * args->range_count * 512) / 1024 / 1024 / elapsed);
    }

    return NULL;
}

int get_eid_list(struct nvme_handle *h, uint32_t nsid, uint16_t *eid_list, int max_eid) {
    struct nvme_fdp_ruhu_log *log = nvme_fdp_get_ruhu_log(h, nsid);
    if (!log) {
        perror("fdp ruhu log failed");
        return -1;
    }

    int count = 0;
    for (int i = 0; i < le16_to_cpu(log->num_ruh); i++) {
        if (count >= max_eid) break;
        eid_list[count++] = log->ruhu[i].reclaim_unit_handle;
    }

    free(log);
    return count;
}

int main(int argc, char **argv) {
    int threads = 1, qdepth = 1, random = 0, eid_auto = 1;
    int range_count = 1, total_iters = 100, quiet = 0;
    uint64_t src_lba = 0, dst_lba = 1024;
    const char *dev_path = "/dev/nvme0n1";

    static struct option opts[] = {
        {"threads", required_argument, 0, 't'},
        {"qdepth", required_argument, 0, 'q'},
        {"count", required_argument, 0, 'c'},
        {"range", required_argument, 0, 'r'},
        {"eid", required_argument, 0, 'e'},
        {"random", no_argument, 0, 'R'},
        {"src-lba", required_argument, 0, 's'},
        {"dst-lba", required_argument, 0, 'd'},
        {"quiet", no_argument, 0, 'z'},
        {"device", required_argument, 0, 'D'},
        {0, 0, 0, 0}
    };

    int opt;
    int user_eid = -1;
    while ((opt = getopt_long(argc, argv, "t:q:c:r:e:s:d:D:Rz", opts, NULL)) != -1) {
        switch (opt) {
            case 't': threads = atoi(optarg); break;
            case 'q': qdepth = atoi(optarg); break;
            case 'c': total_iters = atoi(optarg); break;
            case 'r': range_count = atoi(optarg); break;
            case 'e': user_eid = atoi(optarg); eid_auto = 0; break;
            case 'R': random = 1; break;
            case 's': src_lba = strtoull(optarg, NULL, 0); break;
            case 'd': dst_lba = strtoull(optarg, NULL, 0); break;
            case 'z': quiet = 1; break;
            case 'D': dev_path = optarg; break;
        }
    }

    struct nvme_handle *h = nvme_open(dev_path);
    if (!h) {
        perror("nvme_open");
        return 1;
    }

    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    int nsid = nvme_get_nsid(h);
    uint16_t eid_list[256] = {0};
    int eid_count = get_eid_list(h, nsid, eid_list, 256);
    if (eid_count < 1) {
        fprintf(stderr, "No available FDP EIDs found. Fallback to 0\n");
        eid_list[0] = 0;
        eid_count = 1;
    }

    pthread_t tid[threads];
    thread_args_t args[threads];

    for (int i = 0; i < threads; i++) {
        args[i] = (thread_args_t){
            .thread_id = i,
            .fd = fd,
            .eid = eid_auto ? eid_list[i % eid_count] : user_eid,
            .qdepth = qdepth,
            .random = random,
            .range_count = range_count,
            .total_iters = total_iters,
            .nsid = nsid,
            .src_lba = src_lba,
            .dst_lba = dst_lba,
            .quiet = quiet
        };
        pthread_create(&tid[i], NULL, copy_thread_func, &args[i]);
    }

    int total_success = 0, total_fail = 0;
    double total_time = 0;
    for (int i = 0; i < threads; i++) {
        pthread_join(tid[i], NULL);
        total_success += args[i].copied;
        total_fail += args[i].errors;
        if (args[i].elapsed > total_time) total_time = args[i].elapsed;
    }

    printf("\n[Summary] Threads: %d, Success: %d, Fail: %d\n", threads, total_success, total_fail);
    printf("  Total Time: %.2f sec, Throughput: %.2f MB/s\n",
           total_time,
           (double)(total_success * range_count * 512) / 1024 / 1024 / total_time);

    close(fd);
    nvme_close(h);
    return 0;
}
