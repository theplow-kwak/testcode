#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <libnvme.h>

#define MAX_THREADS 64

typedef struct {
    struct nvme_ctrl *ctrl;
    struct nvme_qpair *qpair;
    uint64_t src_lba;
    uint64_t dst_lba;
    uint32_t num_blocks;
    int nsid;
    int qdepth;
    int thread_id;
    int random_lba;
} thread_ctx_t;

void *copy_worker(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    struct nvme_copy_range range;
    struct timespec start, end;
    uint64_t total_bytes = 0;
    int i, ret;

    ctx->qpair = nvme_create_queue(ctx->ctrl, 0, ctx->qdepth, 0);
    if (!ctx->qpair) {
        fprintf(stderr, "Thread %d: Failed to create queue\n", ctx->thread_id);
        pthread_exit(NULL);
    }

    memset(&range, 0, sizeof(range));
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < ctx->qdepth; i++) {
        if (ctx->random_lba) {
            range.src_slba = rand() % 1000000;
            ctx->dst_lba = rand() % 1000000;
        } else {
            range.src_slba = ctx->src_lba + i * ctx->num_blocks;
        }

        range.nlb = ctx->num_blocks - 1;

        ret = nvme_copy_qpair(ctx->qpair, ctx->nsid, ctx->dst_lba + i * ctx->num_blocks, 1, &range, 0);
        if (ret != 0) {
            fprintf(stderr, "Thread %d: Copy command failed: %s\n", ctx->thread_id, strerror(errno));
        } else {
            total_bytes += (ctx->num_blocks * 512);
        }
    }

    // Wait for completions
    for (i = 0; i < ctx->qdepth; i++) {
        ret = nvme_wait_for_completion(ctx->qpair);
        if (ret != 0) {
            fprintf(stderr, "Thread %d: Completion failed\n", ctx->thread_id);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double time_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Thread %d: Copied %.2f MB in %.2f seconds (%.2f MB/s)\n",
        ctx->thread_id, total_bytes / (1024.0 * 1024.0), time_sec, (total_bytes / (1024.0 * 1024.0)) / time_sec);

    nvme_free_queue(ctx->qpair);

    return NULL;
}

int main(int argc, char **argv) {
    const char *dev_path = "/dev/nvme0n1";
    int nsid = 1;
    int num_threads = 1;
    int qdepth = 1;
    int random_lba = 0;
    uint64_t src_lba = 0;
    uint64_t dst_lba = 0;
    uint32_t num_blocks = 8;
    int opt;

    while ((opt = getopt(argc, argv, "d:n:q:s:t:rT:")) != -1) {
        switch (opt) {
            case 'd': dev_path = optarg; break;
            case 'n': nsid = atoi(optarg); break;
            case 'q': qdepth = atoi(optarg); break;
            case 's': src_lba = strtoull(optarg, NULL, 10); break;
            case 't': dst_lba = strtoull(optarg, NULL, 10); break;
            case 'r': random_lba = 1; break;
            case 'T': num_threads = atoi(optarg); break;
            default:
                fprintf(stderr, "Usage: %s [-d dev_path] [-n nsid] [-q qdepth] [-T threads] [-s src_lba] [-t dst_lba] [-r random_lba]\n", argv[0]);
                exit(1);
        }
    }

    struct nvme_ctrl *ctrl = nvme_open(dev_path);
    if (!ctrl) {
        perror("nvme_open");
        return 1;
    }

    pthread_t threads[MAX_THREADS];
    thread_ctx_t ctx[MAX_THREADS];

    srand(time(NULL));

    for (int i = 0; i < num_threads; i++) {
        ctx[i].ctrl = ctrl;
        ctx[i].nsid = nsid;
        ctx[i].src_lba = src_lba;
        ctx[i].dst_lba = dst_lba;
        ctx[i].num_blocks = num_blocks;
        ctx[i].qdepth = qdepth;
        ctx[i].thread_id = i;
        ctx[i].random_lba = random_lba;
        pthread_create(&threads[i], NULL, copy_worker, &ctx[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    nvme_close(ctrl);

    return 0;
}
