#include <iostream>
#include <vector>
#include <string>
#include <fcntl.h>
#include <coroutine>
#include <stdexcept>
#include <memory>
#include <functional>
#include <charconv>
#include <cstring>
#include <liburing.h>
#include <libnvme.h>

#ifndef IORING_OP_NVME_CMD
#define IORING_OP_NVME_CMD 19
#endif

#if LIBURING_VERSION_MAJOR < 2
static inline void io_uring_prep_nvme_cmd(struct io_uring_sqe *sqe, int fd,
                                          struct nvme_passthru_cmd *cmd)
{
    sqe->opcode = IORING_OP_NVME_CMD;
    sqe->fd = fd;
    sqe->addr = (unsigned long)cmd;
    sqe->len = sizeof(*cmd);
}
#endif

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

struct request
{
    std::coroutine_handle<> handle;
    int cqe_res;
    struct iovec iov;
    std::unique_ptr<char[]> buf;
    struct nvme_passthru_cmd cmd;
};

struct io_awaitable
{
    request *req_ptr;
    io_awaitable(request *r) : req_ptr(r) {}
    bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> h) { req_ptr->handle = h; }
    int await_resume() const
    {
        if (req_ptr->cqe_res < 0)
        {
            throw std::runtime_error(strerror(-req_ptr->cqe_res));
        }
        return req_ptr->cqe_res;
    }
};

class IOHandler
{
public:
    virtual ~IOHandler() = default;
    virtual void prep_read(io_uring *ring, __u64 offset, __u32 len, request *req) = 0;
    virtual void prep_write(io_uring *ring, __u64 offset, __u32 len, request *req) = 0;
    virtual const std::string &get_name() const = 0;
    virtual bool is_block_device() const = 0;
    virtual size_t get_size() const = 0;
};

class FileIOHandler : public IOHandler
{
    std::string path;
    int fd;
    size_t file_size;

public:
    FileIOHandler(const std::string &p, bool is_source) : path(p)
    {
        int flags = is_source ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
        fd = open(path.c_str(), flags, 0644);
        if (fd < 0)
            throw std::runtime_error("Failed to open file: " + path);
        struct stat file_stat;
        if (fstat(fd, &file_stat) < 0)
            throw std::runtime_error("Failed to get file size: " + path);
        file_size = file_stat.st_size;
        std::cout << "File size: " << file_size << " bytes" << std::endl;
    }
    ~FileIOHandler()
    {
        if (fd >= 0)
            close(fd);
    }

    void prep_read(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        req->iov = {.iov_base = req->buf.get(), .iov_len = len};
        io_uring_prep_readv(sqe, fd, &req->iov, 1, offset);
        io_uring_sqe_set_data(sqe, req);
    }

    void prep_write(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        req->iov.iov_len = len;
        io_uring_prep_writev(sqe, fd, &req->iov, 1, offset);
        io_uring_sqe_set_data(sqe, req);
    }
    const std::string &get_name() const override { return path; }
    bool is_block_device() const override { return false; }
    size_t get_size() const override { return file_size; }
};

class NvmeIOHandler : public IOHandler
{
    std::string path;
    int fd;
    const __u32 lba_size = 512;
    size_t dev_size;

public:
    NvmeIOHandler(const std::string &p) : path(p)
    {
        fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
            throw std::runtime_error("Failed to open NVMe device: " + path);
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
            throw std::runtime_error("Failed to identify NVMe device: " + path);
        dev_size = static_cast<size_t>(bytes);
        std::cout << "Device size: " << dev_size << " bytes" << std::endl;
    }
    ~NvmeIOHandler()
    {
        if (fd >= 0)
            close(fd);
    }

    void prep_read(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        auto &cmd = req->cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = nvme_cmd_read;
        cmd.nsid = 1;
        cmd.addr = (__u64)req->buf.get();
        cmd.data_len = len;
        cmd.cdw10 = (offset / lba_size);
        cmd.cdw12 = (len / lba_size) - 1;
        io_uring_prep_nvme_cmd(sqe, fd, &cmd);
        io_uring_sqe_set_data(sqe, req);
    }

    void prep_write(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        auto &cmd = req->cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = nvme_cmd_write;
        cmd.nsid = 1;
        cmd.addr = (__u64)req->buf.get();
        cmd.data_len = len;
        cmd.cdw10 = (offset / lba_size);
        cmd.cdw12 = (len / lba_size) - 1;
        io_uring_prep_nvme_cmd(sqe, fd, &cmd);
        io_uring_sqe_set_data(sqe, req);
    }
    const std::string &get_name() const override { return path; }
    bool is_block_device() const override { return true; }
    size_t get_size() const override { return 0; }
};

task read_and_write_block(struct io_uring *ring, IOHandler &src, IOHandler &dest, __u64 offset, __u32 block_size, std::function<void()> on_complete)
{
    request req;
    req.buf = std::make_unique<char[]>(block_size);

    try
    {
        src.prep_read(ring, offset, block_size, &req);
        int bytes_read = co_await io_awaitable(&req);

        dest.prep_write(ring, offset, bytes_read, &req);
        co_await io_awaitable(&req);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error at offset " << offset << ": " << e.what() << std::endl;
    }
    on_complete();
}

task run_admin_identify(struct io_uring *ring, const std::string &dev_path, std::function<void()> on_complete)
{
    int fd = open(dev_path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        on_complete();
        throw std::runtime_error("Failed to open device for admin cmd: " + dev_path);
    }

    request req;
    req.buf = std::make_unique<char[]>(4096);

    try
    {
        auto &cmd = req.cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = nvme_admin_identify;
        cmd.addr = (__u64)req.buf.get();
        cmd.data_len = 4096;
        cmd.cdw10 = 1;

        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        io_uring_prep_nvme_cmd(sqe, fd, &cmd);
        io_uring_sqe_set_data(sqe, &req);

        std::cout << "Submitting Identify Controller command..." << std::endl;
        co_await io_awaitable(&req);
        std::cout << "Admin command completed." << std::endl;

        std::string model_number(req.buf.get() + 4, 40);
        model_number.erase(model_number.find_last_not_of(' ') + 1);
        std::cout << " > Model Number: " << model_number << std::endl;
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Admin command failed: " << e.what() << std::endl;
    }
    close(fd);
    on_complete();
}
std::unique_ptr<IOHandler> create_handler(const std::string &arg, bool is_source)
{
    size_t pos = arg.find(':');
    if (pos == std::string::npos)
        throw std::runtime_error("Invalid source/destination format. Use 'file:/path' or 'nvme:/dev/path'.");

    std::string type = arg.substr(0, pos);
    std::string path = arg.substr(pos + 1);

    if (type == "file")
    {
        return std::make_unique<FileIOHandler>(path, is_source);
    }
    else if (type == "nvme")
    {
        return std::make_unique<NvmeIOHandler>(path);
    }
    else
    {
        throw std::runtime_error("Unknown type: " + type);
    }
}

void print_usage(const char *prog_name)
{
    std::cerr << "Usage: " << std::endl;
    std::cerr << "  " << prog_name << " copy <source> <destination> <size_mb> [block_size_kb] [queue_depth]" << std::endl;
    std::cerr << "    <source>/<destination>: file:/path/to/file or nvme:/dev/nvme0n1" << std::endl;
    std::cerr << "  " << prog_name << " admin identify <device>" << std::endl;
    std::cerr << "    <device>: /dev/nvme0" << std::endl;
}

// --- ★★★ 수정된 메인 로직 및 이벤트 루프 ★★★ ---

void run_copy_logic(IOHandler &src, IOHandler &dest, __u64 insize, int bs, int qd)
{
    struct io_uring ring;
    struct io_uring_params params = {};

    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;

    if (io_uring_queue_init_params(qd, &ring, &params) < 0)
    {
        params = {}; // SQPOLL 실패 시 플래그 초기화
        if (io_uring_queue_init_params(qd, &ring, &params) < 0)
            throw std::runtime_error("io_uring_queue_init failed.");
        std::cout << "Note: SQPOLL not supported, running in normal mode." << std::endl;
    }

    int inflight = 0;
    __u64 offset = 0;
    bool all_submitted = false;

    std::cout << "Copying " << insize << " bytes from " << src.get_name()
              << " to " << dest.get_name() << "..." << std::endl;

    // 통합 이벤트 루프
    while (inflight > 0 || !all_submitted)
    {
        // 1. 제출 단계: 큐에 공간이 있으면 최대한 제출
        while (inflight < qd && !all_submitted)
        {
            __u64 this_size = (insize - offset < static_cast<__u64>(bs)) ? (insize - offset) : bs;
            if (this_size == 0)
            {
                all_submitted = true;
                break;
            }
            read_and_write_block(&ring, src, dest, offset, this_size, [&]()
                                 { inflight--; });
            offset += this_size;
            inflight++;
            std::cout << "Submitted block at offset " << offset - this_size << " with size " << this_size << " inflight " << inflight << std::endl;
        }

        // 2. 제출 및 대기 결정
        int submitted = 0;
        // 큐가 꽉 찼거나, 모든 작업을 제출했다면 반드시 wait 해야 함
        if (inflight >= qd || (all_submitted && inflight > 0))
        {
            std::cout << "submit_and_wait " << submitted << " requests, inflight: " << inflight << std::endl;
            submitted = io_uring_submit_and_wait(&ring, 1);
            std::cout << "Submitted " << submitted << " requests, inflight: " << inflight << std::endl;
        }
        else
        {
            std::cout << "submit " << submitted << " requests, inflight: " << inflight << std::endl;
            submitted = io_uring_submit(&ring);
            std::cout << "Submitted " << submitted << " requests, inflight: " << inflight << std::endl;
        }

        if (submitted < 0)
        {
            throw std::runtime_error("io_uring_submit failed: " + std::string(strerror(-submitted)));
        }

        // 3. 완료 처리 (CQE 일괄 처리)
        unsigned cqe_head;
        struct io_uring_cqe *cqe;
        io_uring_for_each_cqe(&ring, cqe_head, cqe)
        {
            auto *req = static_cast<request *>(io_uring_cqe_get_data(cqe));
            if (req)
            {
                req->cqe_res = cqe->res;
                req->handle.resume();
            }
        }
        io_uring_cq_advance(&ring, io_uring_cq_ready(&ring));
        std::cout << "Processed CQEs, inflight: " << inflight << std::endl;
    }
    io_uring_queue_exit(&ring);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    try
    {
        if (command == "copy" && argc >= 5)
        {
            auto src_handler = create_handler(argv[2], true);
            auto dest_handler = create_handler(argv[3], false);
            __u64 insize = std::stoull(argv[4]) * 1024 * 1024;
            int bs = (argc >= 6) ? std::stoi(argv[5]) * 1024 : 128 * 1024;
            int qd = (argc >= 7) ? std::stoi(argv[6]) : 16;
            if (src_handler->get_size() < insize)
                insize = src_handler->get_size();

            if ((src_handler->is_block_device() || dest_handler->is_block_device()) && (bs % 512 != 0))
            {
                throw std::runtime_error("Block size must be a multiple of 512 for NVMe devices.");
            }
            run_copy_logic(*src_handler, *dest_handler, insize, bs, qd);
            std::cout << "Copy finished." << std::endl;
        }
        else if (command == "admin" && argc >= 4 && std::string(argv[2]) == "identify")
        {
            struct io_uring ring;
            io_uring_queue_init(1, &ring, 0);
            int inflight = 1;
            run_admin_identify(&ring, argv[3], [&]()
                               { inflight--; });

            // admin 명령을 위한 간단하고 정확한 이벤트 루프
            while (inflight > 0)
            {
                io_uring_submit_and_wait(&ring, 1);
                struct io_uring_cqe *cqe;
                int ret = io_uring_peek_cqe(&ring, &cqe);
                if (ret == 0 && cqe)
                {
                    auto *req = static_cast<request *>(io_uring_cqe_get_data(cqe));
                    req->cqe_res = cqe->res;
                    req->handle.resume();
                    io_uring_cq_advance(&ring, 1);
                }
            }
            io_uring_queue_exit(&ring);
        }
        else
        {
            print_usage(argv[0]);
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}