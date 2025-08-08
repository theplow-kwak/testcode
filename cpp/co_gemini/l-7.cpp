#include <iostream>
#include <vector>
#include <fcntl.h>
#include <liburing.h>
#include <coroutine>
#include <stdexcept>
#include <memory>
#include <functional>
#include <cstring>

// 파일 디스크립터 정의
int infd = -1, outfd = -1;

// 간단한 비동기 작업을 위한 task 타입
struct task
{
    struct promise_type
    {
        task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

// io_uring 작업을 위한 사용자 데이터
struct request
{
    std::coroutine_handle<> handle;
    int cqe_res;
    struct iovec iov;
    std::unique_ptr<char[]> buf;
};

struct io_awaitable
{
    request *req;

    io_awaitable(request *r) : req(r) {}
    bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> h)
    {
        req->handle = h;
        std::cout << "await_suspend: " << h.address() << std::endl;
    }
    int await_resume() const
    {
        if (req->cqe_res < 0)
        {
            throw std::runtime_error(strerror(-req->cqe_res));
        }
        std::cout << "await_resume: " << req->cqe_res << std::endl;
        return req->cqe_res;
    }
};

// 읽기 작업을 위한 Awaitable
struct read_awaitable : public io_awaitable
{
    read_awaitable(struct io_uring *ring, __u64 offset, __u32 len)
        : io_awaitable(new request{
              .buf = std::make_unique<char[]>(len)})
    {
        req->iov = {.iov_base = req->buf.get(), .iov_len = len};
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        io_uring_prep_readv(sqe, infd, &req->iov, 1, offset);
        io_uring_sqe_set_data(sqe, req);
    }
    // 소멸자에서 메모리 해제
    ~read_awaitable() { delete req; }
};

// 쓰기 작업을 위한 Awaitable
struct write_awaitable : public io_awaitable
{
    write_awaitable(struct io_uring *ring, request *read_req, __u32 len, __u64 offset)
        : io_awaitable(read_req) // 읽기 요청의 버퍼를 재사용
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        io_uring_prep_writev(sqe, outfd, &req->iov, 1, offset);
        io_uring_sqe_set_data(sqe, req);
    }
    // 쓰기는 버퍼 소유권이 없으므로 소멸자에서 아무것도 하지 않음
    ~write_awaitable() {}
};

// 한 블록을 읽고 쓰는 비동기 코루틴
task read_and_write_block(struct io_uring *ring, __u64 offset, __u32 block_size, std::function<void()> on_complete)
{
    try
    {
        // 1. 읽기 작업 Awaitable 생성 및 co_await
        std::cout << "before queue_rw_pair read: offset: " << offset << std::endl;
        auto reader = read_awaitable(ring, offset, block_size);
        int bytes_read = co_await reader;
        std::cout << "complete queue_rw_pair read: offset: " << offset << std::endl;

        // 2. 쓰기 작업 Awaitable 생성 및 co_await (읽은 버퍼 재사용)
        auto writer = write_awaitable(ring, reader.req, bytes_read, offset);
        co_await writer;
        std::cout << "complete queue_rw_pair write: offset: " << offset << std::endl;
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error at offset " << offset << ": " << e.what() << std::endl;
    }

    on_complete();
    std::cout << "complete queue_rw_pair: " << std::endl;
}

void run_file_copy(struct io_uring *ring, int bs, int qd, __u64 insize)
{
    __u64 offset = 0;
    int inflight = 0;

    while (offset < insize || inflight > 0)
    {
        while (inflight < qd && offset < insize)
        {
            __u64 this_size = (insize - offset < static_cast<__u64>(bs)) ? (insize - offset) : bs;

            read_and_write_block(ring, offset, this_size, [&]()
                                 { inflight--; });

            std::cout << "read_and_write_block called with offset: " << offset << ", size: " << this_size << ", inflight: " << inflight << std::endl;
            offset += this_size;
            inflight++;
        }

        io_uring_submit(ring);

        struct io_uring_cqe *cqe;
        int wait_count = (offset < insize && inflight > 0) ? 1 : inflight;
        for (int i = 0; i < wait_count; ++i)
        {
            int ret = io_uring_wait_cqe(ring, &cqe);
            if (ret < 0)
            {
                if (-ret != EAGAIN)
                    std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << std::endl;
                break;
            }

            auto *req = static_cast<request *>(io_uring_cqe_get_data(cqe));
            if (req)
            {
                req->cqe_res = cqe->res;
                req->handle.resume();
            }
            io_uring_cqe_seen(ring, cqe);
            std::cout << "Processed CQEs, inflight: " << inflight << std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " <infile> <outfile> <filesize_in_mb>" << std::endl;
        return 1;
    }

    infd = open(argv[1], O_RDONLY);
    if (infd < 0)
    {
        std::cerr << "Failed to open input file: " << strerror(errno) << std::endl;
        return 1;
    }

    outfd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0)
    {
        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
        close(infd);
        return 1;
    }

    __u64 insize = std::stoull(argv[3]) * 1024 * 1024;

    struct stat st;
    if (fstat(infd, &st) < 0)
    {
        std::cerr << "Failed to get file size: " << strerror(errno) << std::endl;
        close(infd);
        close(outfd);
        return 1;
    }

    if (static_cast<__u64>(st.st_size) < insize)
    {
        insize = static_cast<__u64>(st.st_size);
    }

    int bs = 128 * 1024;
    int qd = 32;

    struct io_uring ring;
    if (io_uring_queue_init(qd, &ring, 0) < 0)
    {
        std::cerr << "Failed to initialize io_uring: " << strerror(errno) << std::endl;
        close(infd);
        close(outfd);
        return 1;
    }

    std::cout << "Copying " << insize << " bytes from " << argv[1] << " to " << argv[2] << std::endl;
    run_file_copy(&ring, bs, qd, insize);
    std::cout << "Copy finished." << std::endl;

    io_uring_queue_exit(&ring);
    close(infd);
    close(outfd);
    return 0;
}