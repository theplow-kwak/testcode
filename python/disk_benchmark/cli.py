import argparse
from benchmark import run_benchmark


def main():
    parser = argparse.ArgumentParser(description="Disk Benchmark Tool")
    parser.add_argument("--disk", required=True, help="Disk device path, e.g., /dev/sdb")
    parser.add_argument("--size", default="512", help="Test size in MB (default: 512)")
    parser.add_argument("--bs", default="1M", help="Block size (default: 1M)")
    parser.add_argument("--mode", default="write", choices=["write", "read", "randread", "randwrite"], help="I/O mode")
    parser.add_argument("--outdir", default="results", help="Output directory")
    parser.add_argument("--graph", default="iops", choices=["iops", "bw"], help="Graph type")
    args = parser.parse_args()

    run_benchmark(args)


if __name__ == "__main__":
    main()
