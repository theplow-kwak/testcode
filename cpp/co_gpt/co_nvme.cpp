#include <liburing.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <coroutine>
#include <vector>
#include <atomic>
#include <iostream>
#include <memory>
#include <chrono>
#include <linux/nvme_ioctl.h>
#include <linux/nvme.h>
#include <sys/ioctl.h>

#define QUEUE_DEPTH 64
#define BLOCK_SIZE 4096

std::atomic<int> inflight = 0;
bool exit_flag = false;
uint64_t runtime = 0;

using namespace std::chrono;

uint64_t time_get_ns()
{
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void print_error(const char *fmt, ...) {}
void print_debug(const char *fmt, ...) {}
void print_trace(const char *fmt, ...) {}

struct Task
{
    struct promise_type
    {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

struct IOReadAwaitable
{
    io_uring *ring;
    iovec iov;
    int fd;
    off_t offset;

    IOReadAwaitable(io_uring *ring, int fd, off_t offset, void *buffer, size_t length)
        : ring(ring), fd(fd), offset(offset)
    {
        iov.iov_base = buffer;
        iov.iov_len = length;
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        if (!sqe)
            throw std::runtime_error("Failed to get SQE");

        io_uring_prep_readv(sqe, fd, &iov, 1, offset);
        io_uring_sqe_set_data(sqe, h.address());
        inflight++;
        io_uring_submit(ring);
    }

    void await_resume() {}
};

struct NVMePassthruAwaitable
{
    io_uring *ring;
    int nvme_fd;
    nvme_admin_cmd *cmd;

    NVMePassthruAwaitable(io_uring *ring, int nvme_fd, nvme_admin_cmd *cmd)
        : ring(ring), nvme_fd(nvme_fd), cmd(cmd) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        if (!sqe)
            throw std::runtime_error("Failed to get SQE");

        io_uring_prep_ioctl(sqe, nvme_fd, NVME_IOCTL_ADMIN_CMD, cmd);
        io_uring_sqe_set_data(sqe, h.address());
        inflight++;
        io_uring_submit(ring);
    }

    void await_resume() {}
};

Task queue_read_and_nvme(io_uring *ring, int in_fd, int nvme_fd, size_t size, off_t offset)
{
    void *read_buf = malloc(size);
    memset(read_buf, 0, size);

    // 1. Read from input file
    co_await IOReadAwaitable(ring, in_fd, offset, read_buf, size);

    // 2. Prepare NVMe admin command (example: Vendor specific, or Identify with data)
    auto *cmd = new nvme_admin_cmd;
    memset(cmd, 0, sizeof(*cmd));
    cmd->opcode = 0x06; // Example: Identify
    cmd->nsid = 1;
    cmd->addr = (__u64)(uintptr_t)read_buf;
    cmd->data_len = size;
    cmd->cdw10 = 1; // For example: identify controller
    cmd->timeout_ms = 0;

    // 3. Submit NVMe admin command
    co_await NVMePassthruAwaitable(ring, nvme_fd, cmd);

    free(read_buf);
    delete cmd;
    inflight--;
}

Task copy_file(io_uring *ring, int in_fd, int nvme_fd, int bs, int qd, uint64_t insize)
{
    uint64_t offset = 0;

    while (insize)
    {
        if (runtime && runtime < time_get_ns())
            break;
        if (exit_flag)
            break;

        while (insize && inflight < qd)
        {
            size_t this_size = (bs > insize) ? insize : bs;

            queue_read_and_nvme(ring, in_fd, nvme_fd, this_size, offset);
            offset += this_size;
            insize -= this_size;
        }

        // Process 1 CQE to yield control
        struct io_uring_cqe *cqe;
        if (inflight > 0 && io_uring_wait_cqe(ring, &cqe) == 0)
        {
            io_uring_cqe_seen(ring, cqe);
        }
    }
    co_return;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <input_file> <nvme_device>\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *nvme_path = argv[2];

    int in_fd = open(input_path, O_RDONLY);
    int nvme_fd = open(nvme_path, O_RDWR);
    if (in_fd < 0 || nvme_fd < 0)
    {
        perror("open");
        return 1;
    }

    off_t size = lseek(in_fd, 0, SEEK_END);
    lseek(in_fd, 0, SEEK_SET);

    io_uring ring;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

    copy_file(&ring, in_fd, nvme_fd, BLOCK_SIZE, QUEUE_DEPTH, size);

    // Drain remaining completions
    while (inflight > 0)
    {
        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) == 0)
        {
            io_uring_cqe_seen(&ring, cqe);
            inflight--;
        }
    }

    io_uring_queue_exit(&ring);
    close(in_fd);
    close(nvme_fd);

    printf("Admin command streaming complete.\n");
    return 0;
}