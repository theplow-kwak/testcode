# analyzer.py
import re
import csv
import os


def parse_fio_log(fio_log):
    iops, bw = None, None
    with open(fio_log) as f:
        for line in f:
            if "iops=" in line:
                iops_match = re.search(r"iops=([0-9\.kK]+)", line)
                bw_match = re.search(r"bw=([^,\s]+)", line)
                if iops_match:
                    iops = iops_match.group(1)
                if bw_match:
                    bw = bw_match.group(1)
                break
    return iops, bw


def parse_qemu_bench_log(bench_log):
    metrics = {}
    with open(bench_log) as f:
        for line in f:
            if line.startswith("read") or line.startswith("write"):
                parts = line.strip().split()
                if len(parts) >= 3:
                    op, iops, bw = parts[0], parts[1], parts[2]
                    metrics[f"{op}_iops"] = iops
                    metrics[f"{op}_bw"] = bw
    return metrics


def get_file_size(path):
    return os.path.getsize(path)


def analyze_results(fio_log, raw_img, qcow_img, bench_log, csv_path):
    iops, bw = parse_fio_log(fio_log)
    bench_metrics = parse_qemu_bench_log(bench_log)

    raw_size = get_file_size(raw_img)
    qcow_size = get_file_size(qcow_img)
    ratio_pct = round(qcow_size / raw_size * 100, 2) if raw_size > 0 else 0

    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["step", "metric", "value"])
        writer.writerow(["fio", "iops", iops])
        writer.writerow(["fio", "bw", bw])
        writer.writerow(["image", "raw_size", raw_size])
        writer.writerow(["image", "qcow2_size", qcow_size])
        writer.writerow(["image", "compression_ratio_pct", ratio_pct])

        for k, v in bench_metrics.items():
            writer.writerow(["qemu-img", k, v])
