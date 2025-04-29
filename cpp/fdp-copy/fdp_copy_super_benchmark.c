#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include <libnvme.h>

#define MAX_RANGES 256
#define MAX_THREADS 64

struct thread_args {
    struct nvme_dev *dev;
    __u32 nsid;
    __u64 src_lba_start;
    __u64 dst_lba_start;
    __u16 blocks_per_range;
    __u16 ranges_per_thread;
    __u32 eid;
    int thread_id;
    int qdepth;
};

static double time_diff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

void *copy_worker(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    struct nvme_copy_range *ranges;
    struct nvme_copy_args copy_args;
    __u32 result;
    int ret;

    ranges = calloc(args->ranges_per_thread, sizeof(*ranges));
    if (!ranges) {
        perror("calloc for ranges");
        pthread_exit(NULL);
    }

    for (int j = 0; j < args->ranges_per_thread; j++) {
        ranges[j].slba = args->src_lba_start + j * args->blocks_per_range;
        ranges[j].nlb = args->blocks_per_range - 1;
        memset(ranges[j].rsvd0, 0, sizeof(ranges[j].rsvd0));
        memset(ranges[j].rsvd18, 0, sizeof(ranges[j].rsvd18));
        ranges[j].eilbrt = 0;
        ranges[j].elbat = 0;
        ranges[j].elbatm = 0;
    }

    memset(&copy_args, 0, sizeof(copy_args));
    copy_args.sdlba = args->dst_lba_start;
    copy_args.result = &result;
    copy_args.copy = ranges;
    copy_args.args_size = sizeof(copy_args);
    copy_args.fd = nvme_dev_get_fd(args->dev);
    copy_args.timeout = 0;
    copy_args.nsid = args->nsid;
    copy_args.nr = args->ranges_per_thread;
    copy_args.dspec = args->eid;

    ret = nvme_copy(&copy_args);
    if (ret != 0) {
        fprintf(stderr, "Thread %d: nvme_copy failed: %s\n", args->thread_id, strerror(errno));
    } else {
        printf("Thread %d: nvme_copy succeeded. Result: 0x%x\n", args->thread_id, result);
    }

    free(ranges);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc < 9) {
        fprintf(stderr, "Usage: %s <dev> <nsid> <src_lba> <dst_lba> <blocks_per_range> <ranges_per_thread> <threads> <eid>\n", argv[0]);
        return 1;
    }

    const char *dev_path = argv[1];
    __u32 nsid = strtoul(argv[2], NULL, 0);
    __u64 src_lba_start = strtoull(argv[3], NULL, 0);
    __u64 dst_lba_start = strtoull(argv[4], NULL, 0);
    __u16 blocks_per_range = atoi(argv[5]);
    __u16 ranges_per_thread = atoi(argv[6]);
    int num_threads = atoi(argv[7]);
    __u32 eid = atoi(argv[8]);

    struct nvme_dev *dev = nvme_dev_open(dev_path);
    if (!dev) {
        perror("nvme_dev_open");
        return 1;
    }

    pthread_t threads[MAX_THREADS];
    struct thread_args args[MAX_THREADS];

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    for (int i = 0; i < num_threads; i++) {
        args[i].dev = dev;
        args[i].nsid = nsid;
        args[i].src_lba_start = src_lba_start + i * ranges_per_thread * blocks_per_range;
        args[i].dst_lba_start = dst_lba_start + i * ranges_per_thread * blocks_per_range;
        args[i].blocks_per_range = blocks_per_range;
        args[i].ranges_per_thread = ranges_per_thread;
        args[i].eid = eid;
        args[i].thread_id = i;
        args[i].qdepth = 1;

        if (pthread_create(&threads[i], NULL, copy_worker, &args[i])) {
            perror("pthread_create");
            nvme_dev_close(dev);
            return 1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end_time, NULL);
    double seconds = time_diff(start_time, end_time);

    printf("\nTotal Time: %.3f seconds\n", seconds);

    nvme_dev_close(dev);
    return 0;
}
