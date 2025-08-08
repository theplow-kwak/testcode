#include "util/argparser.hpp"
#include "util/logger.hpp"

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

/* io_uring async commands: */
#define NVME_URING_CMD_IO _IOWR('N', 0x80, struct nvme_uring_cmd)
#define NVME_URING_CMD_IO_VEC _IOWR('N', 0x81, struct nvme_uring_cmd)
#define NVME_URING_CMD_ADMIN _IOWR('N', 0x82, struct nvme_uring_cmd)
#define NVME_URING_CMD_ADMIN_VEC _IOWR('N', 0x83, struct nvme_uring_cmd)

#if LIBURING_VERSION_MAJOR < 2
static inline void io_uring_prep_nvme_cmd(struct io_uring_sqe *sqe, int fd)
{
    sqe->fd = fd;
    sqe->opcode = IORING_OP_URING_CMD;
    sqe->cmd_op = NVME_URING_CMD_ADMIN;
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

enum filetype
{
    FD_TYPE_FILE = 1, /* plain file */
    FD_TYPE_BLOCK,    /* block device */
    FD_TYPE_CHAR,     /* character device */
    FD_TYPE_PIPE,     /* pipe */
};

enum ctrl_mcid
{
    IDENTIFY_CTRL = 0x01,
    IDENTIFY_CHILD_CONTROLLER = 0x02,
    CTRL_MANAGEMENT = 0x03,
    CHILD_CONTROLLER_CONTROL = 0x04,
    GET_SINGLE_CHILD_CONTROLLER_LOG_PAGE = 0x05,
    GET_CHILD_CONTROLLER_ADMIN_COMMANDS_PERMISSION = 0x06,
    SET_CHILD_CONTROLLER_ADMIN_COMMANDS_PERMISSION = 0x07,
    NAMESPACE_PAGE_MAP_OPERATION_COMMAND = 0x08,
    QUERY_NAMESPACE_PAGE_MAP_COMMAND = 0x09,
    NAMESPACE_READ_COMMAND = 0x0a,
    NAMESPACE_WRITE_COMMAND = 0x0b,
    QUERY_CHILD_CONTROLLER_QUEUES_COMMAND = 0x0c,
    SET_CHILD_CONTROLLER_QUEUES_COMMAND = 0x0d,
    ASSOCIATE_CHILD_CONTROLLERS_COMMAND = 0x0e,
};

#define CUST_NODATA 0xD0
#define CUST_HOST_TO_CONTROLLER 0xD1
#define CUST_CONTROLLER_TO_HOST 0xD2
#define CUST_BIDIRECTION 0xD3

struct nvme_data
{
    __u32 nsid;
    __u32 lba_shift;
    __u32 lba_size;
    __u32 lba_ext;
    __u16 lr;
};

class NvmeIOHandler : public IOHandler
{
    std::string path;
    int fd;
    const __u32 lba_size = 512;
    size_t dev_size;
    enum filetype filetype;
    struct nvme_data nvme_data;

public:
    NvmeIOHandler(const std::string &p) : path(p)
    {
        fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
            throw std::runtime_error("Failed to open NVMe device: " + path);
        if (get_file_size() != 0)
            throw std::runtime_error("Failed to identify NVMe device: " + path);
    }
    ~NvmeIOHandler()
    {
        if (fd >= 0)
            close(fd);
    }

    void prep_read(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        auto cmd = (struct nvme_uring_cmd *)sqe->cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd->opcode = CUST_CONTROLLER_TO_HOST;
        cmd->nsid = nvme_data.nsid;
        cmd->addr = (__u64)req->buf.get();
        cmd->data_len = len;
        cmd->cdw10 = offset & 0xffffffff;
        cmd->cdw11 = offset >> 32;
        cmd->cdw12 = len | (nvme_data.lr << 31);
        cmd->cdw15 = NAMESPACE_READ_COMMAND;

        io_uring_prep_nvme_cmd(sqe, fd);
        io_uring_sqe_set_data(sqe, req);
    }

    void prep_write(io_uring *ring, __u64 offset, __u32 len, request *req) override
    {
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        auto cmd = (struct nvme_uring_cmd *)sqe->cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd->opcode = CUST_HOST_TO_CONTROLLER;
        cmd->nsid = nvme_data.nsid;
        cmd->addr = (__u64)req->buf.get();
        cmd->data_len = len;
        cmd->cdw10 = offset & 0xffffffff;
        cmd->cdw11 = offset >> 32;
        cmd->cdw12 = len | (nvme_data.lr << 31);
        cmd->cdw15 = NAMESPACE_WRITE_COMMAND;
        io_uring_prep_nvme_cmd(sqe, fd);
        io_uring_sqe_set_data(sqe, req);
    }

    int get_file_size()
    {
        struct stat st;

        if (fstat(fd, &st) < 0)
            return -1;
        if (S_ISBLK(st.st_mode))
        {
            unsigned long long bytes;

            if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
                return -1;

            dev_size = bytes;
            filetype = FD_TYPE_BLOCK;
            std::cout << path << ": FD_TYPE_BLOCK, size " << dev_size << ", type " << filetype << std::endl;
            return 0;
        }
        else if (S_ISCHR(st.st_mode))
        {
            filetype = FD_TYPE_CHAR;
            dev_size = 0;
            // if (ctrl_get_info(path))
            //     return -1;
            std::cout << path << ": FD_TYPE_CHAR, size " << dev_size << ", type " << filetype << std::endl;
            return 0;
        }

        return -1;
    }
    const std::string &get_name() const override { return path; }
    bool is_block_device() const override { return true; }
    size_t get_size() const override { return dev_size; }
};

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
        io_uring_sqe *sqe = io_uring_get_sqe(ring);
        auto cmd = (struct nvme_uring_cmd *)sqe->cmd;
        memset(cmd, 0, sizeof(struct nvme_uring_cmd));
        cmd->opcode = nvme_admin_identify;
        cmd->nsid = 0;
        cmd->addr = (__u64)req.buf.get();
        cmd->data_len = 4096;
        cmd->cdw10 = NVME_IDENTIFY_CNS_CTRL;

        io_uring_prep_nvme_cmd(sqe, fd);
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

void run_copy_logic(IOHandler &src, IOHandler &dest, __u64 insize, int bs, int qd)
{
    struct io_uring ring;
    struct io_uring_params params = {};

    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 20000;

    std::cout << "try io_uring_queue_init_params: flags " << params.flags << std::endl;
    if (io_uring_queue_init_params(qd, &ring, &params) < 0)
    {
        params = {}; // SQPOLL 실패 시 플래그 초기화
        std::cout << "try io_uring_queue_init_params: flags " << params.flags << std::endl;
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

            std::cout << "read_and_write_block called with offset: " << offset << ", size: " << this_size << ", inflight: " << inflight << std::endl;
            offset += this_size;
            inflight++;
        }

        // 2. 제출 및 대기 결정
        int submitted = 0;
        // 큐가 꽉 찼거나, 모든 작업을 제출했다면 반드시 wait 해야 함
        if (inflight >= qd || (all_submitted && inflight > 0))
        {
            std::cout << "try io_uring_submit_and_wait: flags " << params.flags << " inflight: " << inflight << std::endl;
            submitted = io_uring_submit_and_wait(&ring, 1);
        }
        else
        {
            std::cout << "try io_uring_submit: flags " << params.flags << " inflight: " << inflight << std::endl;
            submitted = io_uring_submit(&ring);
        }

        std::cout << "Submitted " << submitted << " requests, inflight: " << inflight << std::endl;
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
    std::cout << "Copy finished." << std::endl;
    io_uring_queue_exit(&ring);
}

int main(int argc, char *argv[])
{
    Logger logger(LogLevel::DEBUG);

    ArgParser parser("Copy using io_uring. ver.0.1.0");
    parser.add_positional("source", "Source file or device path.", true);
    parser.add_option("--nsid", "-i", "Specifie the target Child Controller ID.", true);
    parser.add_option("--lr", "-l", "Limited Retry (LR): 1-limited retry efforts, 0-apply all available error recovery", false, "0");
    parser.add_option("--slba", "-s", "64-bit address of the first logical block", true);
    parser.add_option("--nlb", "-n", "The number of LBAs to return", false);
    parser.add_option("--bs", "-c", "block size", false, "512");
    parser.add_option("--depth", "-d", "io depth", false, "64");
    parser.add_option("--time", "-t", "test time (unit: min)", false, "2");
    parser.add_option("--log", "-L", "log level", false, "INFO");
    if (!parser.parse(argc, argv))
    {
        return 1;
    }

    std::string command = argv[1];

    try
    {
        auto source = parser.get("source").value();
        auto src_handler = create_handler(source, true);
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
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}