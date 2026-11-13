"""Plot the Tube / Mech transfer curves and the Excite envelope step response.

    crush_nulltest --curves <dir>
    python tools/plot_curves.py <dir>

Needs matplotlib. Writes tube_curve.png, mech_curve.png and excite_envelope.png.
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read(path):
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    header, data = rows[0], rows[1:]
    cols = list(zip(*[[float(v) for v in r] for r in data]))
    return header, cols


def curve(directory, name, title):
    header, cols = read(os.path.join(directory, name + ".csv"))
    fig, ax = plt.subplots(figsize=(6, 5))
    for i in range(1, len(header)):
        ax.plot(cols[0], cols[i], label=header[i].replace("p", "p = ") + " %")
    ax.plot(cols[0], cols[0], color="0.7", lw=0.8, ls="--", label="identity")
    ax.set_xlabel("input")
    ax.set_ylabel("output")
    ax.set_title(title)
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(directory, name + ".png"), dpi=150)


def envelope(directory):
    header, cols = read(os.path.join(directory, "excite_envelope.csv"))
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(cols[0], cols[1], label="detector input")
    ax.plot(cols[0], cols[2], label="envelope")
    ax.plot(cols[0], [g / 10 for g in cols[3]], label="gain / 10 (cap 3)", ls="--")
    ax.set_xlabel("sample (44.1 kHz)")
    ax.set_title("Excite envelope step response")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(directory, "excite_envelope.png"), dpi=150)


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    curve(d, "tube_curve", "Tube transfer curve")
    curve(d, "mech_curve", "Mech transfer curve")
    envelope(d)
    print("wrote PNGs to", d)
