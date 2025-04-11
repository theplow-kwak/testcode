import csv
import matplotlib.pyplot as plt


def generate_plot(csv_path, mode, out_path):
    ops = []
    values = []

    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["step"] == "qemu-img" and mode in row["metric"]:
                ops.append(row["metric"].split("_")[0])
                values.append(float(row["value"]))

    if not ops:
        print("[!] No data to plot.")
        return

    plt.figure(figsize=(6, 4))
    plt.bar(ops, values, color="skyblue")
    plt.title(f"QCOW2 {mode.upper()} Performance")
    plt.xlabel("Operation")
    plt.ylabel(mode.upper())
    plt.tight_layout()
    plt.savefig(out_path)
    plt.close()
    print(f"[*] Plot saved to {out_path}")
