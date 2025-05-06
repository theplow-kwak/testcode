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
#include <pthread.h>
// #include <linux/nvme_uring.h>

#include "common.h"
#include "nvme.h"
#include <liburing.h>
#include "libnvme.h"
#include "nvme-print.h"

#define CREATE_CMD
#include "fdp.h"

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

#define COPY_CHUNK_SIZE 64

struct async_copy_task
{
    struct nvme_copy_args args;
    int result;
    int assigned;
    int done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

void *copy_worker(void *arg)
{
    struct async_copy_task *task = (struct async_copy_task *)arg;
    while (1)
    {
        pthread_mutex_lock(&task->lock);
        while (!task->assigned)
        {
            printf("Worker waiting for task assignment...\n");
            pthread_cond_wait(&task->cond, &task->lock);
        }
        printf("Worker assigned a task. sdlba %lld, nr %ld\n", task->args.sdlba, task->args.nr);

        task->assigned = 0;
        pthread_mutex_unlock(&task->lock);

        // 실제 NVMe Copy 명령 실행
        printf("Executing NVMe Copy command...\n");
        task->result = nvme_copy(&task->args);
        printf("NVMe Copy command execution completed with result: %d\n", task->result);

        pthread_mutex_lock(&task->lock);
        task->done = 1;
        pthread_cond_signal(&task->cond);
        printf("Worker marked task as done and signaled condition.\n");
        pthread_mutex_unlock(&task->lock);
    }
    return NULL;
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
    const char *d_qdepth = "io_uring queue depth (동시 처리 개수)";

    _cleanup_nvme_dev_ struct nvme_dev *dev = NULL;
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

    union
    {
        struct nvme_copy_range f0[256];
        struct nvme_copy_range_f1 f1[256];
        struct nvme_copy_range_f2 f2[256];
        struct nvme_copy_range_f3 f3[256];
    } *copy;

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
        OPT_INT("qdepth", 'Q', &cfg.qdepth, d_qdepth),
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
    printf("nr = %d, nb = %d, ns = %d, nids = %d, nrts = %d, natms = %d, nats = %d\n",
           nr, nb, ns, nids, nrts, natms, nats);
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

    int chunk_size = COPY_CHUNK_SIZE;
    int qdepth = cfg.qdepth;
    int remain = nb;
    int off = 0;
    int ret = 0;
    int submitted = 0, completed = 0;

    struct async_copy_task *tasks = calloc(qdepth, sizeof(struct async_copy_task));
    pthread_t *threads = calloc(qdepth, sizeof(pthread_t));
    if (!tasks || !threads)
    {
        nvme_show_error("memory alloc failed");
        return -ENOMEM;
    }
    for (int i = 0; i < qdepth; ++i)
    {
        pthread_mutex_init(&tasks[i].lock, NULL);
        pthread_cond_init(&tasks[i].cond, NULL);
        tasks[i].assigned = 0;
        tasks[i].done = 0;
        pthread_create(&threads[i], NULL, copy_worker, &tasks[i]);
    }

    while (remain > 0 || completed < submitted)
    {
        // 1. 할당 가능한 빈 스레드 찾기
        for (int i = 0; i < qdepth && remain > 0; ++i)
        {
            pthread_mutex_lock(&tasks[i].lock);
            if (!tasks[i].assigned && tasks[i].done)
            {
                pthread_mutex_unlock(&tasks[i].lock);
                continue;
            }
            if (!tasks[i].assigned && !tasks[i].done)
            {
                // 새 chunk 할당
                int this_chunk = (remain > chunk_size) ? chunk_size : remain;
                void *chunk_copy = nvme_alloc(sizeof(*copy));
                if (!chunk_copy)
                {
                    pthread_mutex_unlock(&tasks[i].lock);
                    ret = -ENOMEM;
                    break;
                }
                if (cfg.format == 0)
                    fdp_init_copy_range((struct nvme_copy_range *)chunk_copy, &nlbs[off], &slbas[off], &eilbrts.short_pi[off], &elbatms[off], &elbats[off], this_chunk);
                else if (cfg.format == 1)
                    fdp_init_copy_range_f1((struct nvme_copy_range_f1 *)chunk_copy, &nlbs[off], &slbas[off], &eilbrts.long_pi[off], &elbatms[off], &elbats[off], this_chunk);
                else if (cfg.format == 2)
                    fdp_init_copy_range_f2((struct nvme_copy_range_f2 *)chunk_copy, &snsids[off], &nlbs[off], &slbas[off], &sopts[off], &eilbrts.short_pi[off], &elbatms[off], &elbats[off], this_chunk);
                else if (cfg.format == 3)
                    fdp_init_copy_range_f3((struct nvme_copy_range_f3 *)chunk_copy, &snsids[off], &nlbs[off], &slbas[off], &sopts[off], &eilbrts.long_pi[off], &elbatms[off], &elbats[off], this_chunk);
                else
                {
                    pthread_mutex_unlock(&tasks[i].lock);
                    nvme_show_error("invalid format");
                    ret = -EINVAL;
                    break;
                }
                tasks[i].args = (struct nvme_copy_args){
                    .args_size = sizeof(tasks[i].args),
                    .fd = dev_fd(dev),
                    .nsid = cfg.namespace_id,
                    .copy = chunk_copy,
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
                tasks[i].assigned = 1;
                tasks[i].done = 0;
                pthread_cond_signal(&tasks[i].cond);
                pthread_mutex_unlock(&tasks[i].lock);
                remain -= this_chunk;
                off += this_chunk;
                submitted++;
            }
            else
            {
                pthread_mutex_unlock(&tasks[i].lock);
            }
        }
        // 2. 완료된 작업 수집
        for (int i = 0; i < qdepth; ++i)
        {
            pthread_mutex_lock(&tasks[i].lock);
            if (tasks[i].done)
            {
                // 결과 처리 (에러 체크 등)
                if (tasks[i].result < 0)
                {
                    nvme_show_error("NVMe Copy: %s", nvme_strerror(-tasks[i].result));
                    ret = tasks[i].result;
                }
                free(tasks[i].args.copy);
                tasks[i].done = 0;
                completed++;
            }
            pthread_mutex_unlock(&tasks[i].lock);
        }
    }
    // 스레드 종료 및 정리
    for (int i = 0; i < qdepth; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
        pthread_mutex_destroy(&tasks[i].lock);
        pthread_cond_destroy(&tasks[i].cond);
    }
    free(tasks);
    free(threads);
    if (!ret)
        printf("NVMe Copy: success\n");
    return ret;
}
