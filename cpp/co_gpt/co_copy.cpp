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

#define QUEUE_DEPTH 64
#define BLOCK_SIZE 4096

// inflight는 atomic으로 관리
std::atomic<int> inflight = 0;
bool exit_flag = false;
uint64_t runtime = 0;

using namespace std::chrono;

uint64_t time_get_ns()
{
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void print_debug(const char *fmt, ...) {}
void print_error(const char *fmt, ...) {}
void print_trace(const char *fmt, ...) {}
void print_info(const char *fmt, ...) {}

struct Task
{
    struct promise_type
    {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

struct IOAwaitable
{
    io_uring *ring;
    iovec iov;
    int fd;
    off_t offset;
    bool is_read;

    IOAwaitable(io_uring *ring, int fd, off_t offset, void *buffer, size_t length, bool is_read)
        : ring(ring), fd(fd), offset(offset), is_read(is_read)
    {
        iov.iov_base = buffer;
        iov.iov_len = length;
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        if (!sqe)
        {
            throw std::runtime_error("Failed to get SQE");
        }

        if (is_read)
        {
            io_uring_prep_readv(sqe, fd, &iov, 1, offset);
        }
        else
        {
            io_uring_prep_writev(sqe, fd, &iov, 1, offset);
        }

        io_uring_sqe_set_data(sqe, h.address());
        inflight++;
        io_uring_submit(ring);
    }

    void await_resume()
    {
        inflight--;
    }
};

Task queue_rw_pair(io_uring *ring, int in_fd, int out_fd, size_t size, off_t offset)
{
    void *read_buf = malloc(size);
    void *write_buf = malloc(size);

    // 읽기
    co_await IOAwaitable(ring, in_fd, offset, read_buf, size, true);
    // 쓰기
    // memcpy(write_buf, read_buf, size);
    co_await IOAwaitable(ring, out_fd, offset, read_buf, size, false);

    free(read_buf);
    free(write_buf);
}

Task copy_file(io_uring *ring, int in_fd, int out_fd, int bs, int qd, uint64_t insize)
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
            size_t this_size = bs;
            if (this_size > insize)
                this_size = insize;

            queue_rw_pair(ring, in_fd, out_fd, this_size, offset);
            offset += this_size;
            insize -= this_size;
        }

        // 완료된 CQE 처리
        struct io_uring_cqe *cqe;
        while (inflight > 0)
        {
            int ret = io_uring_wait_cqe(ring, &cqe);
            if (ret < 0)
            {
                print_error("io_uring_wait_cqe error\n");
                co_return;
            }
            io_uring_cqe_seen(ring, cqe);
            break; // 하나만 처리하고 돌아가서 다시 루프를 돔
        }
    }
    co_return;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <src> <dst>\n", argv[0]);
        return 1;
    }

    const char *src_path = argv[1];
    const char *dst_path = argv[2];

    int in_fd = open(src_path, O_RDONLY);
    int out_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in_fd < 0 || out_fd < 0)
    {
        perror("open");
        return 1;
    }

    off_t size = lseek(in_fd, 0, SEEK_END);
    lseek(in_fd, 0, SEEK_SET);

    io_uring ring;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

    copy_file(&ring, in_fd, out_fd, BLOCK_SIZE, QUEUE_DEPTH, size);

    // 모든 CQE 완료될 때까지 기다림
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
    close(out_fd);

    printf("File copy complete.\n");
    return 0;
}