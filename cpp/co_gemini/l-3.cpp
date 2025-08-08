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
    size_t get_size() const override { return dev_size; }
};

// --- 코루틴 로직 ---

// ★★★ 수정 1: io_uring* ring을 파라미터로 다시 받도록 변경 ★★★
task read_and_write_block(struct io_uring *ring, IOHandler &src, IOHandler &dest, __u64 offset, __u32 block_size, std::function<void()> on_complete)
{
    request req;
    req.buf = std::make_unique<char[]>(block_size);

    try
    {
        std::cout << "before queue_rw_pair read: offset: " << offset << std::endl;
        src.prep_read(ring, offset, block_size, &req);
        int bytes_read = co_await io_awaitable(&req);
        std::cout << "complete queue_rw_pair read: offset: " << offset << std::endl;

        dest.prep_write(ring, offset, bytes_read, &req);
        co_await io_awaitable(&req);
        std::cout << "complete queue_rw_pair write: offset: " << offset << std::endl;
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

// --- 이벤트 루프 및 main (변경 없음) ---

void run_event_loop(struct io_uring *ring, int &inflight_ops)
{
    while (inflight_ops > 0)
    {
        int ret = io_uring_submit(ring);
        if (ret < 0)
        {
            std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
        }

        struct io_uring_cqe *cqe;
        ret = io_uring_wait_cqe(ring, &cqe);
        if (ret < 0)
        {
            if (-ret != EAGAIN)
                std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << std::endl;
            // 에러가 발생해도 계속 진행하여 남은 요청을 처리
            inflight_ops--;
            continue;
        }

        auto *req = static_cast<request *>(io_uring_cqe_get_data(cqe));
        if (req)
        {
            req->cqe_res = cqe->res;
            req->handle.resume();
        }
        io_uring_cqe_seen(ring, cqe);
    }
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

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    struct io_uring ring;
    int qd = 256;

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

            io_uring_queue_init(qd, &ring, 0);
            std::cout << "Copying " << insize << " bytes from " << src_handler->get_name()
                      << " to " << dest_handler->get_name() << std::endl;

            int inflight = 0;
            __u64 offset = 0;

            while (offset < insize)
            {
                while (inflight < qd && offset < insize)
                {
                    __u32 this_size = (insize - offset < bs) ? (insize - offset) : bs;
                    // ★★★ 수정 3: ring 포인터를 명시적으로 전달 ★★★
                    read_and_write_block(&ring, *src_handler, *dest_handler, offset, this_size, [&]()
                                         { inflight--; });
                    offset += this_size;
                    inflight++;
                }
                run_event_loop(&ring, inflight);
            }

            std::cout << "Copy finished." << std::endl;
        }
        else if (command == "admin" && argc >= 4 && std::string(argv[2]) == "identify")
        {
            io_uring_queue_init(qd, &ring, 0);
            int inflight = 1;
            run_admin_identify(&ring, argv[3], [&]()
                               { inflight--; });
            run_event_loop(&ring, inflight);
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
        if (ring.ring_fd)
            io_uring_queue_exit(&ring);
        return 1;
    }

    io_uring_queue_exit(&ring);
    return 0;
}