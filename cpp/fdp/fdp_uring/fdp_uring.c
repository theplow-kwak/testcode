// SPDX-License-Identifier: GPL-2.0-or-later

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <string.h>
// #include <linux/nvme_uring.h>

#include "common.h"
#include "nvme.h"
#include <liburing.h>
#include "libnvme.h"
#include "nvme-print.h"

#define CREATE_CMD
#include "fdp_uring.h"

static int fdp_configs(int argc, char **argv, struct command *cmd,
                       struct plugin *plugin)
{
    const char *desc = "Get Flexible Data Placement Configurations";
    const char *egid = "Endurance group identifier";
    const char *human_readable = "show log in readable format";
    const char *raw = "use binary output";

    nvme_print_flags_t flags;
    struct nvme_dev *dev;
    struct nvme_fdp_config_log hdr;
    void *log = NULL;
    int err;

    struct config
    {
        __u16 egid;
        char *output_format;
        bool human_readable;
        bool raw_binary;
    };

    struct config cfg = {
        .egid = 0,
        .output_format = "normal",
        .raw_binary = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("endgrp-id", 'e', &cfg.egid, egid),
        OPT_FMT("output-format", 'o', &cfg.output_format, output_format),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
        OPT_FLAG("human-readable", 'H', &cfg.human_readable, human_readable),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    err = validate_output_format(cfg.output_format, &flags);
    if (err < 0)
        goto out;

    if (cfg.raw_binary)
        flags = BINARY;

    if (cfg.human_readable)
        flags |= VERBOSE;

    if (!cfg.egid)
    {
        fprintf(stderr, "endurance group identifier required\n");
        err = -EINVAL;
        goto out;
    }

    err = nvme_get_log_fdp_configurations(dev->direct.fd, cfg.egid, 0,
                                          sizeof(hdr), &hdr);
    if (err)
    {
        nvme_show_status(errno);
        goto out;
    }

    log = malloc(hdr.size);
    if (!log)
    {
        err = -ENOMEM;
        goto out;
    }

    err = nvme_get_log_fdp_configurations(dev->direct.fd, cfg.egid, 0,
                                          hdr.size, log);
    if (err)
    {
        nvme_show_status(errno);
        goto out;
    }

    nvme_show_fdp_configs(log, hdr.size, flags);

out:
    dev_close(dev);
    free(log);

    return err;
}

static int fdp_usage(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Get Flexible Data Placement Reclaim Unit Handle Usage";
    const char *egid = "Endurance group identifier";
    const char *raw = "use binary output";

    nvme_print_flags_t flags;
    struct nvme_dev *dev;
    struct nvme_fdp_ruhu_log hdr;
    size_t len;
    void *log = NULL;
    int err;

    struct config
    {
        __u16 egid;
        char *output_format;
        bool raw_binary;
    };

    struct config cfg = {
        .egid = 0,
        .output_format = "normal",
        .raw_binary = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("endgrp-id", 'e', &cfg.egid, egid),
        OPT_FMT("output-format", 'o', &cfg.output_format, output_format),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    err = validate_output_format(cfg.output_format, &flags);
    if (err < 0)
        goto out;

    if (cfg.raw_binary)
        flags = BINARY;

    err = nvme_get_log_reclaim_unit_handle_usage(dev->direct.fd, cfg.egid,
                                                 0, sizeof(hdr), &hdr);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    len = sizeof(hdr) + le16_to_cpu(hdr.nruh) * sizeof(struct nvme_fdp_ruhu_desc);
    log = malloc(len);
    if (!log)
    {
        err = -ENOMEM;
        goto out;
    }

    err = nvme_get_log_reclaim_unit_handle_usage(dev->direct.fd, cfg.egid,
                                                 0, len, log);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    nvme_show_fdp_usage(log, len, flags);

out:
    dev_close(dev);
    free(log);

    return err;
}

static int fdp_stats(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Get Flexible Data Placement Statistics";
    const char *egid = "Endurance group identifier";
    const char *raw = "use binary output";

    nvme_print_flags_t flags;
    struct nvme_dev *dev;
    struct nvme_fdp_stats_log stats;
    int err;

    struct config
    {
        __u16 egid;
        char *output_format;
        bool raw_binary;
    };

    struct config cfg = {
        .egid = 0,
        .output_format = "normal",
        .raw_binary = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("endgrp-id", 'e', &cfg.egid, egid),
        OPT_FMT("output-format", 'o', &cfg.output_format, output_format),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    err = validate_output_format(cfg.output_format, &flags);
    if (err < 0)
        goto out;

    if (cfg.raw_binary)
        flags = BINARY;

    if (!cfg.egid)
    {
        fprintf(stderr, "endurance group identifier required\n");
        err = -EINVAL;
        goto out;
    }

    memset(&stats, 0x0, sizeof(stats));

    err = nvme_get_log_fdp_stats(dev->direct.fd, cfg.egid, 0, sizeof(stats), &stats);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    nvme_show_fdp_stats(&stats, flags);

out:
    dev_close(dev);

    return err;
}

static int fdp_events(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Get Flexible Data Placement Events";
    const char *egid = "Endurance group identifier";
    const char *host_events = "Get host events";
    const char *raw = "use binary output";

    nvme_print_flags_t flags;
    struct nvme_dev *dev;
    struct nvme_fdp_events_log events;
    int err;

    struct config
    {
        __u16 egid;
        bool host_events;
        char *output_format;
        bool raw_binary;
    };

    struct config cfg = {
        .egid = 0,
        .host_events = false,
        .output_format = "normal",
        .raw_binary = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("endgrp-id", 'e', &cfg.egid, egid),
        OPT_FLAG("host-events", 'E', &cfg.host_events, host_events),
        OPT_FMT("output-format", 'o', &cfg.output_format, output_format),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    err = validate_output_format(cfg.output_format, &flags);
    if (err < 0)
        goto out;

    if (cfg.raw_binary)
        flags = BINARY;

    if (!cfg.egid)
    {
        fprintf(stderr, "endurance group identifier required\n");
        err = -EINVAL;
        goto out;
    }

    memset(&events, 0x0, sizeof(events));

    err = nvme_get_log_fdp_events(dev->direct.fd, cfg.egid,
                                  cfg.host_events, 0, sizeof(events), &events);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    nvme_show_fdp_events(&events, flags);

out:
    dev_close(dev);

    return err;
}

static int fdp_status(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Reclaim Unit Handle Status";
    const char *namespace_id = "Namespace identifier";
    const char *raw = "use binary output";

    nvme_print_flags_t flags;
    struct nvme_dev *dev;
    struct nvme_fdp_ruh_status hdr;
    size_t len;
    void *buf = NULL;
    int err = -1;

    struct config
    {
        __u32 namespace_id;
        char *output_format;
        bool raw_binary;
    };

    struct config cfg = {
        .output_format = "normal",
        .raw_binary = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("namespace-id", 'n', &cfg.namespace_id, namespace_id),
        OPT_FMT("output-format", 'o', &cfg.output_format, output_format),
        OPT_FLAG("raw-binary", 'b', &cfg.raw_binary, raw),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    err = validate_output_format(cfg.output_format, &flags);
    if (err < 0)
        goto out;

    if (cfg.raw_binary)
        flags = BINARY;

    if (!cfg.namespace_id)
    {
        err = nvme_get_nsid(dev_fd(dev), &cfg.namespace_id);
        if (err < 0)
        {
            perror("get-namespace-id");
            goto out;
        }
    }

    err = nvme_fdp_reclaim_unit_handle_status(dev_fd(dev),
                                              cfg.namespace_id, sizeof(hdr), &hdr);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    len = sizeof(struct nvme_fdp_ruh_status) +
          le16_to_cpu(hdr.nruhsd) * sizeof(struct nvme_fdp_ruh_status_desc);
    buf = malloc(len);
    if (!buf)
    {
        err = -ENOMEM;
        goto out;
    }

    err = nvme_fdp_reclaim_unit_handle_status(dev_fd(dev),
                                              cfg.namespace_id, len, buf);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    nvme_show_fdp_ruh_status(buf, len, flags);

out:
    free(buf);
    dev_close(dev);

    return err;
}

static int fdp_update(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Reclaim Unit Handle Update";
    const char *namespace_id = "Namespace identifier";
    const char *_pids = "Comma-separated list of placement identifiers to update";

    struct nvme_dev *dev;
    unsigned short pids[256];
    __u16 buf[256];
    int npids;
    int err = -1;

    struct config
    {
        __u32 namespace_id;
        char *pids;
    };

    struct config cfg = {
        .pids = "",
    };

    OPT_ARGS(opts) = {
        OPT_UINT("namespace-id", 'n', &cfg.namespace_id, namespace_id),
        OPT_LIST("pids", 'p', &cfg.pids, _pids),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    npids = argconfig_parse_comma_sep_array_short(cfg.pids, pids, ARRAY_SIZE(pids));
    if (npids < 0)
    {
        perror("could not parse pids");
        err = -EINVAL;
        goto out;
    }
    else if (npids == 0)
    {
        fprintf(stderr, "no placement identifiers set\n");
        err = -EINVAL;
        goto out;
    }

    if (!cfg.namespace_id)
    {
        err = nvme_get_nsid(dev_fd(dev), &cfg.namespace_id);
        if (err < 0)
        {
            perror("get-namespace-id");
            goto out;
        }
    }

    for (unsigned int i = 0; i < npids; i++)
        buf[i] = cpu_to_le16(pids[i]);

    err = nvme_fdp_reclaim_unit_handle_update(dev_fd(dev), cfg.namespace_id, npids, buf);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    printf("update: Success\n");

out:
    dev_close(dev);

    return err;
}

static int fdp_set_events(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Enable or disable FDP events";
    const char *namespace_id = "Namespace identifier";
    const char *enable = "Enable/disable event";
    const char *event_types = "Comma-separated list of event types";
    const char *ph = "Placement Handle";
    const char *save = "specifies that the controller shall save the attribute";

    struct nvme_dev *dev;
    int err = -1;
    unsigned short evts[255];
    int nev;
    __u8 buf[255];

    struct config
    {
        __u32 namespace_id;
        __u16 ph;
        char *event_types;
        bool enable;
        bool save;
    };

    struct config cfg = {
        .enable = false,
        .save = false,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("namespace-id", 'n', &cfg.namespace_id, namespace_id),
        OPT_SHRT("placement-handle", 'p', &cfg.ph, ph),
        OPT_FLAG("enable", 'e', &cfg.enable, enable),
        OPT_FLAG("save", 's', &cfg.save, save),
        OPT_LIST("event-types", 't', &cfg.event_types, event_types),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    nev = argconfig_parse_comma_sep_array_short(cfg.event_types, evts, ARRAY_SIZE(evts));
    if (nev < 0)
    {
        perror("could not parse event types");
        err = -EINVAL;
        goto out;
    }
    else if (nev == 0)
    {
        fprintf(stderr, "no event types set\n");
        err = -EINVAL;
        goto out;
    }
    else if (nev > 255)
    {
        fprintf(stderr, "too many event types (max 255)\n");
        err = -EINVAL;
        goto out;
    }

    if (!cfg.namespace_id)
    {
        err = nvme_get_nsid(dev_fd(dev), &cfg.namespace_id);
        if (err < 0)
        {
            if (errno != ENOTTY)
            {
                fprintf(stderr, "get-namespace-id: %s\n", nvme_strerror(errno));
                goto out;
            }

            cfg.namespace_id = NVME_NSID_ALL;
        }
    }

    for (unsigned int i = 0; i < nev; i++)
        buf[i] = (__u8)evts[i];

    struct nvme_set_features_args args = {
        .args_size = sizeof(args),
        .fd = dev_fd(dev),
        .fid = NVME_FEAT_FID_FDP_EVENTS,
        .save = cfg.save,
        .nsid = cfg.namespace_id,
        .cdw11 = (nev << 16) | cfg.ph,
        .cdw12 = cfg.enable ? 0x1 : 0x0,
        .data_len = sizeof(buf),
        .data = buf,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .result = NULL,
    };

    err = nvme_set_features(&args);
    if (err)
    {
        nvme_show_status(err);
        goto out;
    }

    printf("set-events: Success\n");

out:
    dev_close(dev);

    return err;
}

static int fdp_feature(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "Show, enable or disable FDP configuration";
    const char *enable_conf_idx = "FDP configuration index to enable";
    const char *endurance_group = "Endurance group ID";
    const char *disable = "Disable current FDP configuration";

    _cleanup_nvme_dev_ struct nvme_dev *dev = NULL;
    int err = -1;
    __u32 result;
    bool enabling_conf_idx = false;
    struct nvme_set_features_args setf_args = {
        .args_size = sizeof(setf_args),
        .fd = -1,
        .fid = NVME_FEAT_FID_FDP,
        .save = 1,
        .nsid = 0,
        .data_len = 0,
        .data = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
    };

    struct config
    {
        bool disable;
        __u8 fdpcidx;
        __u16 endgid;
    };

    struct config cfg = {
        .disable = false,
        .fdpcidx = 0,
        .endgid = 0,
    };

    OPT_ARGS(opts) = {
        OPT_SHRT("endgrp-id", 'e', &cfg.endgid, endurance_group),
        OPT_BYTE("enable-conf-idx", 'c', &cfg.fdpcidx, enable_conf_idx),
        OPT_FLAG("disable", 'd', &cfg.disable, disable),
        OPT_INCR("verbose", 'v', &nvme_cfg.verbose, verbose),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    enabling_conf_idx = argconfig_parse_seen(opts, "enable-conf-idx");
    if (enabling_conf_idx && cfg.disable)
    {
        nvme_show_error("Cannot enable and disable at the same time");
        return -EINVAL;
    }

    if (!enabling_conf_idx && !cfg.disable)
    {
        struct nvme_get_features_args getf_args = {
            .args_size = sizeof(getf_args),
            .fd = dev_fd(dev),
            .fid = NVME_FEAT_FID_FDP,
            .nsid = 0,
            .sel = NVME_GET_FEATURES_SEL_CURRENT,
            .cdw11 = cfg.endgid,
            .uuidx = 0,
            .data_len = 0,
            .data = NULL,
            .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
            .result = &result,
        };

        nvme_show_result("Endurance Group                               : %d", cfg.endgid);

        err = nvme_get_features(&getf_args);
        if (err)
        {
            nvme_show_status(err);
            return err;
        }

        nvme_show_result("Flexible Direct Placement Enable (FDPE)       : %s",
                         (result & 0x1) ? "Yes" : "No");
        nvme_show_result("Flexible Direct Placement Configuration Index : %u",
                         (result >> 8) & 0xf);
        return err;
    }

    setf_args.fd = dev_fd(dev);
    setf_args.cdw11 = cfg.endgid;
    setf_args.cdw12 = cfg.fdpcidx << 8 | (!cfg.disable);

    err = nvme_set_features(&setf_args);
    if (err)
    {
        nvme_show_status(err);
        return err;
    }
    nvme_show_result("Success %s Endurance Group: %d, FDP configuration index: %d",
                     (cfg.disable) ? "disabling" : "enabling", cfg.endgid, cfg.fdpcidx);
    return err;
}

#define COPY_CHUNK_SIZE 2048

static inline void fdp_init_copy_range_elbt(__u8 *elbt, __u64 eilbrt)
{
    int i;

    for (i = 0; i < 8; i++)
        elbt[9 - i] = (eilbrt >> (8 * i)) & 0xff;
    elbt[1] = 0;
    elbt[0] = 0;
}

static __u64 fdp_init_copy_range(struct nvme_copy_range *copy, __u64 *nlbs,
                                 __u64 *slbas, __u32 *eilbrts, __u32 *elbatms,
                                 __u32 *elbats, __u16 nr, __u16 chunk, __u64 offset)
{
    int i;
    int nlb = 0;
    __u64 nlb_total = 0;

    for (i = 0; i < nr; i++)
    {
        nlb = min(cpu_to_le16(nlbs[i]), chunk);
        copy[i].nlb = nlb - 1;
        copy[i].slba = cpu_to_le64(slbas[i]) + offset;
        copy[i].eilbrt = cpu_to_le32(eilbrts[i]);
        copy[i].elbatm = cpu_to_le16(elbatms[i]);
        copy[i].elbat = cpu_to_le16(elbats[i]);
        nlb_total += nlb;
        nlbs[i] -= nlb;
    }
    return nlb_total;
}

static __u64 fdp_init_copy_range_f1(struct nvme_copy_range_f1 *copy, __u64 *nlbs,
                                    __u64 *slbas, __u64 *eilbrts, __u32 *elbatms,
                                    __u32 *elbats, __u16 nr, __u16 chunk, __u64 offset)
{
    int i;
    __u64 nlb_total = 0;

    for (i = 0; i < nr; i++)
    {
        copy[i].nlb = min(cpu_to_le16(nlbs[i]), chunk);
        copy[i].slba = cpu_to_le64(slbas[i]) + offset;
        copy[i].elbatm = cpu_to_le16(elbatms[i]);
        copy[i].elbat = cpu_to_le16(elbats[i]);
        fdp_init_copy_range_elbt(copy[i].elbt, eilbrts[i]);
        nlb_total += copy[i].nlb;
        nlbs[i] -= copy[i].nlb;
    }
    return nlb_total;
}

static __u64 fdp_init_copy_range_f2(struct nvme_copy_range_f2 *copy, __u32 *snsids,
                                    __u64 *nlbs, __u64 *slbas, __u16 *sopts,
                                    __u32 *eilbrts, __u32 *elbatms, __u32 *elbats,
                                    __u16 nr, __u16 chunk, __u64 offset)
{
    int i;
    __u64 nlb_total = 0;

    for (i = 0; i < nr; i++)
    {
        copy[i].snsid = cpu_to_le32(snsids[i]);
        copy[i].nlb = min(cpu_to_le16(nlbs[i]), chunk);
        copy[i].slba = cpu_to_le64(slbas[i]) + offset;
        copy[i].sopt = cpu_to_le16(sopts[i]);
        copy[i].eilbrt = cpu_to_le32(eilbrts[i]);
        copy[i].elbatm = cpu_to_le16(elbatms[i]);
        copy[i].elbat = cpu_to_le16(elbats[i]);
        nlb_total += copy[i].nlb;
        nlbs[i] -= copy[i].nlb;
    }
    return nlb_total;
}

static __u64 fdp_init_copy_range_f3(struct nvme_copy_range_f3 *copy, __u32 *snsids,
                                    __u64 *nlbs, __u64 *slbas, __u16 *sopts,
                                    __u64 *eilbrts, __u32 *elbatms, __u32 *elbats,
                                    __u16 nr, __u16 chunk, __u64 offset)
{
    int i;
    __u64 nlb_total = 0;

    for (i = 0; i < nr; i++)
    {
        copy[i].snsid = cpu_to_le32(snsids[i]);
        copy[i].nlb = min(cpu_to_le16(nlbs[i]), chunk);
        copy[i].slba = cpu_to_le64(slbas[i]) + offset;
        copy[i].sopt = cpu_to_le16(sopts[i]);
        copy[i].elbatm = cpu_to_le16(elbatms[i]);
        copy[i].elbat = cpu_to_le16(elbats[i]);
        fdp_init_copy_range_elbt(copy[i].elbt, eilbrts[i]);
        nlb_total += copy[i].nlb;
        nlbs[i] -= copy[i].nlb;
    }
    return nlb_total;
}

static __u64 time_get_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static inline int identify_ns(int fd, __u32 nsid, void *data)
{
    struct nvme_identify_args args = {
        .result = NULL,
        .data = data,
        .args_size = sizeof(args),
        .fd = fd,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .cns = NVME_IDENTIFY_CNS_NS,
        .csi = NVME_CSI_NVM,
        .nsid = nsid,
        .cntid = NVME_CNTLID_NONE,
        .cns_specific_id = NVME_CNSSPECID_NONE,
        .uuidx = NVME_UUID_NONE,
    };

    return nvme_identify(&args);
}

static inline void io_uring_prep_uring_cmd(struct io_uring_sqe *sqe, int fd, void *cmd, unsigned int cmdlen, unsigned int flags)
{
    sqe->opcode = IORING_OP_URING_CMD;
    sqe->fd = fd;
    sqe->off = 0;
    sqe->addr = (unsigned long)cmd;
    sqe->len = cmdlen;
    sqe->uring_cmd_flags = flags;
    sqe->cmd_op = NVME_URING_CMD_IO;
}

static int nvme_copy_io_uring(struct io_uring *ring, struct nvme_copy_args *args)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return -1;

    size_t data_len = 0;
    if (args->format == 1)
        data_len = args->nr * sizeof(struct nvme_copy_range_f1);
    else if (args->format == 2)
        data_len = args->nr * sizeof(struct nvme_copy_range_f2);
    else if (args->format == 3)
        data_len = args->nr * sizeof(struct nvme_copy_range_f3);
    else
        data_len = args->nr * sizeof(struct nvme_copy_range);

    __u32 cdw3 = 0;
    __u32 cdw12 = ((args->nr - 1) & 0xff) | ((args->format & 0xf) << 8) |
                  ((args->prinfor & 0xf) << 12) | ((args->dtype & 0xf) << 20) |
                  ((args->prinfow & 0xf) << 26) | ((args->fua & 0x1) << 30) |
                  ((args->lr & 0x1) << 31);
    __u32 cdw14 = 0;

    if (args->args_size == sizeof_args(struct nvme_copy_args, format, __u64))
    {
        cdw3 = 0;
        cdw14 = args->ilbrt;
    }
    else
    {
        cdw3 = (args->ilbrt_u64 >> 32) & 0xffffffff;
        cdw14 = args->ilbrt_u64 & 0xffffffff;
    }

    struct nvme_uring_cmd *cmd = calloc(1, sizeof(struct nvme_uring_cmd));
    if (!cmd)
        return -1;

    cmd->opcode = 0x19; // NVMe Copy opcode
    cmd->nsid = args->nsid;
    cmd->addr = (__u64)(uintptr_t)args->copy;
    cmd->data_len = data_len;
    cmd->cdw3 = cdw3;
    cmd->cdw10 = args->sdlba & 0xffffffff;
    cmd->cdw11 = args->sdlba >> 32;
    cmd->cdw12 = cdw12;
    cmd->cdw13 = (args->dspec & 0xffff) << 16;
    cmd->cdw14 = cdw14;
    cmd->cdw15 = (args->lbatm << 16) | args->lbat;

    io_uring_prep_uring_cmd(sqe, args->fd, cmd, sizeof(*cmd), 0);
    io_uring_sqe_set_data(sqe, cmd); // cmd 포인터 저장
    return 0;
}

static int copy_cmd(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    const char *desc = "The Copy command is used by the host to copy data\n"
                       "from one or more source logical block ranges to a\n"
                       "single consecutive destination logical block range.";
    const char *d_sdlba = "64-bit addr of first destination logical block";
    const char *d_slbas = "64-bit addr of first block per range (comma-separated list)";
    const char *d_nlbs = "number of blocks per range (comma-separated list, zeroes-based values)";
    const char *d_snsids = "source namespace identifier per range (comma-separated list)";
    const char *d_sopts = "source options per range (comma-separated list)";
    const char *d_lr = "limited retry";
    const char *d_fua = "force unit access";
    const char *d_prinfor = "protection information and check field (read part)";
    const char *d_prinfow = "protection information and check field (write part)";
    const char *d_ilbrt = "initial lba reference tag (write part)";
    const char *d_eilbrts = "expected lba reference tags (read part, comma-separated list)";
    const char *d_lbat = "lba application tag (write part)";
    const char *d_elbats = "expected lba application tags (read part, comma-separated list)";
    const char *d_lbatm = "lba application tag mask (write part)";
    const char *d_elbatms = "expected lba application tag masks (read part, comma-separated list)";
    const char *d_dtype = "directive type (write part)";
    const char *d_dspec = "directive specific (write part)";
    const char *d_format = "source range entry format";
    const char *d_qdepth = "io_uring queue depth (number of concurrent requests)";

    _cleanup_nvme_dev_ struct nvme_dev *dev = NULL;
    _cleanup_free_ struct nvme_id_ns *id_ns = NULL;
    __u16 nr, nb, ns, nrts, natms, nats, nids;
    __u64 nlbs[256] = {0};
    __u64 slbas[256] = {0};
    __u32 snsids[256] = {0};
    __u16 sopts[256] = {0};
    int err;

    union
    {
        __u32 short_pi[256];
        __u64 long_pi[256];
    } eilbrts;

    __u32 elbatms[256] = {0};
    __u32 elbats[256] = {0};

    struct config
    {
        __u32 namespace_id;
        __u64 sdlba;
        char *slbas;
        char *nlbs;
        char *snsids;
        char *sopts;
        bool lr;
        bool fua;
        __u8 prinfow;
        __u8 prinfor;
        __u64 ilbrt;
        char *eilbrts;
        __u16 lbat;
        char *elbats;
        __u16 lbatm;
        char *elbatms;
        __u8 dtype;
        __u16 dspec;
        __u8 format;
        int qdepth;
        int chunk;
    };

    struct config cfg = {
        .namespace_id = 1,
        .sdlba = 0,
        .slbas = "",
        .nlbs = "",
        .snsids = "",
        .sopts = "",
        .lr = false,
        .fua = false,
        .prinfow = 0,
        .prinfor = 0,
        .ilbrt = 0,
        .eilbrts = "",
        .lbat = 0,
        .elbats = "",
        .lbatm = 0,
        .elbatms = "",
        .dtype = 0,
        .dspec = 0,
        .format = 0,
        .qdepth = 4,
        .chunk = 256,
    };

    OPT_ARGS(opts) = {
        OPT_UINT("namespace-id", 'n', &cfg.namespace_id, "identifier of desired namespace"),
        OPT_SUFFIX("sdlba", 'd', &cfg.sdlba, d_sdlba),
        OPT_LIST("slbs", 's', &cfg.slbas, d_slbas),
        OPT_LIST("blocks", 'b', &cfg.nlbs, d_nlbs),
        OPT_LIST("snsids", 'N', &cfg.snsids, d_snsids),
        OPT_LIST("sopts", 'O', &cfg.sopts, d_sopts),
        OPT_FLAG("limited-retry", 'l', &cfg.lr, d_lr),
        OPT_FLAG("force-unit-access", 'f', &cfg.fua, d_fua),
        OPT_BYTE("prinfow", 'p', &cfg.prinfow, d_prinfow),
        OPT_BYTE("prinfor", 'P', &cfg.prinfor, d_prinfor),
        OPT_SUFFIX("ref-tag", 'r', &cfg.ilbrt, d_ilbrt),
        OPT_LIST("expected-ref-tags", 'R', &cfg.eilbrts, d_eilbrts),
        OPT_SHRT("app-tag", 'a', &cfg.lbat, d_lbat),
        OPT_LIST("expected-app-tags", 'A', &cfg.elbats, d_elbats),
        OPT_SHRT("app-tag-mask", 'm', &cfg.lbatm, d_lbatm),
        OPT_LIST("expected-app-tag-masks", 'M', &cfg.elbatms, d_elbatms),
        OPT_BYTE("dir-type", 'T', &cfg.dtype, d_dtype),
        OPT_SHRT("dir-spec", 'S', &cfg.dspec, d_dspec),
        OPT_BYTE("format", 'F', &cfg.format, d_format),
        OPT_INT("chunk", 'c', &cfg.chunk, "chunk size"),
        OPT_INT("qdepth", 'Q', &cfg.qdepth, d_qdepth),
        OPT_INCR("verbose", 'v', &nvme_cfg.verbose, verbose),
        OPT_END()};

    err = parse_and_open(&dev, argc, argv, desc, opts);
    if (err)
        return err;

    nb = argconfig_parse_comma_sep_array_u64(cfg.nlbs, nlbs, ARRAY_SIZE(nlbs));
    ns = argconfig_parse_comma_sep_array_u64(cfg.slbas, slbas, ARRAY_SIZE(slbas));
    nids = argconfig_parse_comma_sep_array_u32(cfg.snsids, snsids, ARRAY_SIZE(snsids));
    argconfig_parse_comma_sep_array_u16(cfg.sopts, sopts, ARRAY_SIZE(sopts));

    if (cfg.format == 0 || cfg.format == 2)
    {
        nrts = argconfig_parse_comma_sep_array_u32(cfg.eilbrts, eilbrts.short_pi,
                                                   ARRAY_SIZE(eilbrts.short_pi));
    }
    else if (cfg.format == 1 || cfg.format == 3)
    {
        nrts = argconfig_parse_comma_sep_array_u64(cfg.eilbrts, eilbrts.long_pi,
                                                   ARRAY_SIZE(eilbrts.long_pi));
    }
    else
    {
        nvme_show_error("invalid format");
        return -EINVAL;
    }

    natms = argconfig_parse_comma_sep_array_u32(cfg.elbatms, elbatms, ARRAY_SIZE(elbatms));
    nats = argconfig_parse_comma_sep_array_u32(cfg.elbats, elbats, ARRAY_SIZE(elbats));

    nr = max(nb, max(ns, max(nrts, max(natms, nats))));
    if (cfg.format == 2 || cfg.format == 3)
    {
        if (nr != nids)
        {
            nvme_show_error("formats 2 and 3 require source namespace ids for each source range");
            return -EINVAL;
        }
    }
    else if (nids)
    {
        nvme_show_error("formats 0 and 1 do not support cross-namespace copy");
        return -EINVAL;
    }
    if (!cfg.namespace_id)
    {
        err = nvme_get_nsid(dev_fd(dev), &cfg.namespace_id);
        if (err < 0)
        {
            nvme_show_error("get-namespace-id: %s", nvme_strerror(errno));
            return err;
        }
    }

    id_ns = nvme_alloc(sizeof(*id_ns));
    if (!id_ns)
        return -ENOMEM;

    err = identify_ns(dev_fd(dev), cfg.namespace_id, id_ns);
    if (err)
    {
        nvme_show_status(err);
        return err;
    }

    if (!nr || nr > id_ns->msrc + 1)
    {
        nvme_show_error("invalid range: nr(%d) cannot be greater than MSRC(%d)", nr, id_ns->msrc);
        return -EINVAL;
    }

    long long remain = 0;
    long long total_blocks = nlbs[0];
    for (int i = 0; i < nr; ++i)
    {
        if (nlbs[i] < total_blocks)
            nlbs[i] = total_blocks;
        remain += nlbs[i];
    }
    total_blocks = remain;

    int chunk_size = min(min(cfg.chunk, id_ns->mssrl), id_ns->mcl / nr);
    int qdepth = cfg.qdepth;
    int off = 0;
    int ret = 0;
    int submitted = 0, completed = 0, inflight = 0;
    __u64 time_tag = 0;

    size_t copy_size = 0;
    if (cfg.format == 0)
        copy_size = sizeof(struct nvme_copy_range) * nr;
    else if (cfg.format == 1)
        copy_size = sizeof(struct nvme_copy_range_f1) * nr;
    else if (cfg.format == 2)
        copy_size = sizeof(struct nvme_copy_range_f2) * nr;
    else if (cfg.format == 3)
        copy_size = sizeof(struct nvme_copy_range_f3) * nr;

    void **copy_buffers = calloc(qdepth, sizeof(void *));
    if (!copy_buffers)
    {
        nvme_show_error("memory alloc failed");
        return -ENOMEM;
    }
    for (int i = 0; i < qdepth; ++i)
    {
        copy_buffers[i] = nvme_alloc(copy_size);
        if (!copy_buffers[i])
        {
            nvme_show_error("memory alloc failed");
            for (int j = 0; j < i; ++j)
                free(copy_buffers[j]);
            free(copy_buffers);
            return -ENOMEM;
        }
    }

    struct io_uring uring;

    if (io_uring_queue_init(cfg.qdepth, &uring, 0) < 0)
    {
        nvme_show_error("io_uring_queue_init failed");
        return -1;
    }

    while ((remain > 0 || inflight > 0) && ret == 0)
    {
        int submitted_this_round = 0;
        // submit
        while (remain > 0 && inflight < cfg.qdepth)
        {
            int this_chunk = (remain > chunk_size) ? chunk_size : remain;
            int copyed = 0;
            int buffer_index = submitted % cfg.qdepth;
            if (cfg.format == 0)
                copyed = fdp_init_copy_range((struct nvme_copy_range *)copy_buffers[buffer_index], nlbs, slbas, eilbrts.short_pi, elbatms, elbats, nr, this_chunk, off);
            else if (cfg.format == 1)
                copyed = fdp_init_copy_range_f1((struct nvme_copy_range_f1 *)copy_buffers[buffer_index], nlbs, slbas, eilbrts.long_pi, elbatms, elbats, nr, this_chunk, off);
            else if (cfg.format == 2)
                copyed = fdp_init_copy_range_f2((struct nvme_copy_range_f2 *)copy_buffers[buffer_index], snsids, nlbs, slbas, sopts, eilbrts.short_pi, elbatms, elbats, nr, this_chunk, off);
            else if (cfg.format == 3)
                copyed = fdp_init_copy_range_f3((struct nvme_copy_range_f3 *)copy_buffers[buffer_index], snsids, nlbs, slbas, sopts, eilbrts.long_pi, elbatms, elbats, nr, this_chunk, off);
            struct nvme_copy_args args = {
                .args_size = sizeof(args),
                .fd = dev_fd(dev),
                .nsid = cfg.namespace_id,
                .copy = copy_buffers[buffer_index],
                .sdlba = cfg.sdlba + off,
                .nr = nr,
                .prinfor = cfg.prinfor,
                .prinfow = cfg.prinfow,
                .dtype = cfg.dtype,
                .dspec = cfg.dspec,
                .format = cfg.format,
                .lr = cfg.lr,
                .fua = cfg.fua,
                .ilbrt_u64 = cfg.ilbrt,
                .lbatm = cfg.lbatm,
                .lbat = cfg.lbat,
                .timeout = nvme_cfg.timeout,
                .result = NULL,
            };
            printf("[io_uring] SUBMIT: fd=%d nsid=%u sdlba=0x%llx nr=%d chunk_size=%d remain=%lld off=%d\n",
                   args.fd, args.nsid, (unsigned long long)args.sdlba, args.nr, this_chunk, remain, off);
            if (nvme_copy_io_uring(&uring, &args) < 0)
            {
                printf("[io_uring] ERROR: submit failed\n");
                nvme_show_error("nvme_copy_io_uring submit failed");
                // free(chunk_copy);
                ret = -1;
                break;
            }
            inflight++;
            submitted++;
            submitted_this_round++;
            remain -= this_chunk;
            off += this_chunk;
        }
        if (submitted_this_round > 0)
            io_uring_submit(&uring);
        if (ret)
            break;
        if (inflight == 0)
            break;
        // wait completion
        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&uring, &cqe) < 0)
        {
            printf("[io_uring] ERROR: wait_cqe failed\n");
            ret = -1;
            break;
        }
        void *chunk_copy = io_uring_cqe_get_data(cqe);
        printf("[io_uring] COMPLETE: CQE res=%d\n", cqe->res);
        io_uring_cqe_seen(&uring, cqe);
        // free(chunk_copy);
        if (cqe->res < 0)
        {
            printf("[io_uring] ERROR: CQE error: %s\n", strerror(-cqe->res));
            nvme_show_error("NVMe Copy CQE error: %s", strerror(-cqe->res));
            ret = cqe->res;
            break;
        }
        inflight--;
        completed++;
    }
    io_uring_queue_exit(&uring);
    if (!ret)
        printf("NVMe Copy: success\n");
    return ret;
}
