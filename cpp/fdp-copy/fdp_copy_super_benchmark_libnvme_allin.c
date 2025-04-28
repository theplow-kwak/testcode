// fdp_copy_super_benchmark_libnvme_allin.c

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <libnvme.h>

#define MAX_THREADS 128
#define MAX_QDEPTH 128
#define MAX_LATENCIES (MAX_THREADS * MAX_QDEPTH * 1000) // 엄청 크게 잡음

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
    int cpu_affinity;
    int latency_enabled;
    int prp_chain_mode;
    uint64_t latencies[MAX_LATENCIES];
    int latency_count;
    uint64_t total_bytes;
    double total_time;
} thread_ctx_t;

typedef struct {
    const char *dev_path;
    int nsid;
    int num_threads;
    int qdepth;
    int random_lba;
    int cpu_affinity;
    int latency_enabled;
    int prp_chain_mode;
    const char *report_file;
    int csv_enabled;
    uint64_t src_lba;
    uint64_t dst_lba;
    uint32_t num_blocks;
} benchmark_opts_t;

benchmark_opts_t g_opts = {
    .dev_path = "/dev/nvme0n1",
    .nsid = 1,
    .num_threads = 1,
    .qdepth = 1,
    .random_lba = 0,
    .cpu_affinity = 1,
    .latency_enabled = 1,
    .prp_chain_mode = 0,
    .report_file = "fdp_copy_report.csv",
    .csv_enabled = 1,
    .src_lba = 0,
    .dst_lba = 0,
    .num_blocks = 8,
};

void parse_args(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "d:n:q:T:s:t:rcaLPf:")) != -1) {
        switch (opt) {
            case 'd': g_opts.dev_path = optarg; break;
            case 'n': g_opts.nsid = atoi(optarg); break;
            case 'q': g_opts.qdepth = atoi(optarg); break;
            case 'T': g_opts.num_threads = atoi(optarg); break;
            case 's': g_opts.src_lba = strtoull(optarg, NULL, 10); break;
            case 't': g_opts.dst_lba = strtoull(optarg, NULL, 10); break;
            case 'r': g_opts.random_lba = 1; break;
            case 'c': g_opts.cpu_affinity = 0; break;
            case 'a': g_opts.cpu_affinity = 1; break;
            case 'L': g_opts.latency_enabled = 0; break;
            case 'P': g_opts.prp_chain_mode = 1; break;
            case 'f': g_opts.report_file = optarg; break;
            default:
                fprintf(stderr, "Usage: %s [-d dev_path] [-n nsid] [-q qdepth] [-T threads] [-s src_lba] [-t dst_lba] [-r (random)] [-a (affinity)] [-L (no latency)] [-P (prp chain)] [-f report_file]\n", argv[0]);
                exit(1);
        }
    }
}

void *thread_fn(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    cpu_set_t cpuset;
    struct timespec start_time, end_time;
    uint64_t bytes_transferred = 0;
    double total_sec = 0.0;

    if (g_opts.cpu_affinity) {
        CPU_ZERO(&cpuset);
        CPU_SET(ctx->thread_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    // Open controller and create queue
    ctx->ctrl = nvme_open(g_opts.dev_path);
    if (!ctx->ctrl) {
        perror("nvme_open");
        pthread_exit(NULL);
    }

    ctx->qpair = nvme_create_qpair(ctx->ctrl, NVME_QPAIR_QD(g_opts.qdepth));
    if (!ctx->qpair) {
        perror("nvme_create_qpair");
        nvme_close(ctx->ctrl);
        pthread_exit(NULL);
    }

    srand(time(NULL) + ctx->thread_id); // Random seed

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    int submitted = 0, completed = 0;
    uint64_t total_ios = 100000; // 원하는 총 IO 수

    while (completed < total_ios) {
        // Submit new IOs if there is room
        while (submitted - completed < g_opts.qdepth && submitted < total_ios) {
            uint64_t slba = g_opts.src_lba;
            uint64_t dlba = g_opts.dst_lba;

            if (g_opts.random_lba) {
                slba += rand() % 1000000;
                dlba += rand() % 1000000;
            } else {
                slba += submitted * g_opts.num_blocks;
                dlba += submitted * g_opts.num_blocks;
            }

            struct nvme_copy_range range = {
                .slba = slba,
                .nlb = g_opts.num_blocks - 1,
                .eid = 0,
                .elbat = 0,
                .elbaf = 0,
            };

            struct nvme_copy_args args = {
                .nr = 1,
                .ranges = &range,
                .control = 0,
                .dsmgmt = 0,
                .prinf = 0,
                .ref_tag = 0,
                .app_tag = 0,
                .app_tag_mask = 0,
            };

            struct timespec io_start, io_end;
            if (g_opts.latency_enabled)
                clock_gettime(CLOCK_MONOTONIC, &io_start);

            int err = nvme_copy(ctx->qpair, g_opts.nsid, dlba, &args, NULL);

            if (err) {
                fprintf(stderr, "nvme_copy failed: %s\n", strerror(errno));
                continue;
            }

            if (g_opts.latency_enabled) {
                clock_gettime(CLOCK_MONOTONIC, &io_end);
                uint64_t usec = (io_end.tv_sec - io_start.tv_sec) * 1000000 +
                                (io_end.tv_nsec - io_start.tv_nsec) / 1000;
                ctx->latencies[ctx->latency_count++] = usec;
            }

            submitted++;
            bytes_transferred += (g_opts.num_blocks * 512);
        }

        // Wait for completions
        int c = nvme_process_completions(ctx->qpair, 0);
        if (c < 0) {
            fprintf(stderr, "nvme_process_completions failed\n");
            break;
        }
        completed += c;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    ctx->total_time = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    ctx->total_bytes = bytes_transferred;

    nvme_delete_qpair(ctx->qpair);
    nvme_close(ctx->ctrl);

    pthread_exit(NULL);
}

void save_latency_csv(thread_ctx_t *ctxs, int nthreads) {
    FILE *fp = fopen(g_opts.report_file, "w");
    if (!fp) {
        perror("fopen report file");
        return;
    }

    if (g_opts.csv_enabled)
        fprintf(fp, "ThreadID,Latency(us)\n");

    for (int i = 0; i < nthreads; i++) {
        for (int j = 0; j < ctxs[i].latency_count; j++) {
            fprintf(fp, "%d,%lu\n", i, ctxs[i].latencies[j]);
        }
    }
    fclose(fp);
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    pthread_t threads[MAX_THREADS];
    thread_ctx_t ctxs[MAX_THREADS];
    memset(ctxs, 0, sizeof(ctxs));

    for (int i = 0; i < g_opts.num_threads; i++) {
        ctxs[i].nsid = g_opts.nsid;
        ctxs[i].qdepth = g_opts.qdepth;
        ctxs[i].thread_id = i;
        ctxs[i].src_lba = g_opts.src_lba;
        ctxs[i].dst_lba = g_opts.dst_lba;
        ctxs[i].num_blocks = g_opts.num_blocks;
        ctxs[i].random_lba = g_opts.random_lba;
        ctxs[i].cpu_affinity = g_opts.cpu_affinity;
        ctxs[i].latency_enabled = g_opts.latency_enabled;
        ctxs[i].prp_chain_mode = g_opts.prp_chain_mode;

        if (pthread_create(&threads[i], NULL, thread_fn, &ctxs[i]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    uint64_t total_bytes = 0;
    double total_sec = 0.0;

    for (int i = 0; i < g_opts.num_threads; i++) {
        pthread_join(threads[i], NULL);
        total_bytes += ctxs[i].total_bytes;
        if (ctxs[i].total_time > total_sec)
            total_sec = ctxs[i].total_time;
    }

    double mbps = (total_bytes / (1024.0 * 1024.0)) / total_sec;
    double iops = (total_bytes / 512.0) / total_sec;

    printf("====== FDP Copy Super Benchmark Result ======\n");
    printf("Total Threads : %d\n", g_opts.num_threads);
    printf("QDepth per Thread : %d\n", g_opts.qdepth);
    printf("Total Throughput : %.2f MB/s\n", mbps);
    printf("Total IOPS : %.2f\n", iops);
    printf("Total Time : %.2f sec\n", total_sec);

    if (g_opts.latency_enabled) {
        save_latency_csv(ctxs, g_opts.num_threads);
        printf("Latency report saved to %s\n", g_opts.report_file);
    }

    return 0;
}
