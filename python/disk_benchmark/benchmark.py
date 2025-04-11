import os
import subprocess
import time
from analyzer import analyze_results
from visualizer import generate_plot


def run_command(cmd, capture_output=False):
    print(f"[*] Running: {' '.join(cmd)}")
    start = time.time()
    result = subprocess.run(cmd, capture_output=capture_output, text=True)
    end = time.time()
    return result.stdout if capture_output else None, end - start


def run_benchmark(args):
    os.makedirs(args.outdir, exist_ok=True)
    raw_img = os.path.join(args.outdir, "raw.img")
    qcow_img = os.path.join(args.outdir, "qcow2.img")
    fio_log = os.path.join(args.outdir, "fio.log")
    bench_log = os.path.join(args.outdir, "bench.log")
    csv_path = os.path.join(args.outdir, "report.csv")

    # 1. Run fio
    with open(fio_log, "w") as f:
        subprocess.run(
            [
                "fio",
                f"--name=test",
                f"--filename={args.disk}",
                "--direct=1",
                f"--rw={args.mode}",
                f"--bs={args.bs}",
                f"--size={args.size}M",
                "--numjobs=1",
                "--runtime=10s",
                "--group_reporting",
            ],
            stdout=f,
        )

    # 2. dd copy
    _, dd_time = run_command(["dd", f"if={args.disk}", f"of={raw_img}", f"bs={args.bs}", f"count={args.size}", "status=none", "conv=fsync"])

    # 3. convert to qcow2
    _, conv_time = run_command(["qemu-img", "convert", "-c", "-f", "raw", "-O", "qcow2", raw_img, qcow_img])

    # 4. qemu-img bench
    with open(bench_log, "w") as f:
        subprocess.run(["qemu-img", "bench", "-c", "1000", "-d", "64", "-f", "qcow2", "-n", "-s", "4096", "-t", "none", qcow_img], stdout=f)

    # 5. Analyze results
    analyze_results(fio_log, raw_img, qcow_img, bench_log, csv_path)

    # 6. Plot graph
    generate_plot(csv_path, args.graph, os.path.join(args.outdir, "perf.png"))

    print("[*] Benchmark complete.")
