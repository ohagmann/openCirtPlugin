"""Summen-CSVs des letzten Gesamtlaufs gegen eine Baseline vergleichen.

Aufruf:  python oc_summen_vergleich.py [<Baseline-Ordner>]
Ohne Argument wird der juengste Ordner %TEMP%\OpenCirt_summen_baseline_* genommen.
Verglichen wird %TEMP%\OpenCirt_extract\*Summe*.csv (aktueller Lauf).
"""
import os, sys, glob, csv

temp = os.environ["TEMP"]
cur_dir = os.path.join(temp, "OpenCirt_extract")
if len(sys.argv) > 1:
    base_dir = sys.argv[1]
else:
    cands = sorted(glob.glob(os.path.join(temp, "OpenCirt_summen_baseline_*")))
    if not cands:
        sys.exit("Keine Baseline gefunden.")
    base_dir = cands[-1]

def load(path):
    rows = {}
    with open(path, encoding="utf-8", newline="") as f:
        header = None
        for line in f:
            line = line.rstrip("\n")
            if not line: continue
            if line.startswith("#PLANKOPF"):
                rows["#PLANKOPF"] = line; continue
            parts = line.split(";")
            if header is None:
                header = parts; rows["#HEADER"] = line; continue
            rows[parts[0]] = parts
    return header, rows

names = sorted(set(os.path.basename(p) for p in glob.glob(os.path.join(base_dir, "*Summe*.csv")))
             | set(os.path.basename(p) for p in glob.glob(os.path.join(cur_dir, "*Summe*.csv"))))
print(f"Baseline: {base_dir}\nAktuell:  {cur_dir}\n")
total_diff = 0
for n in names:
    bp, cp = os.path.join(base_dir, n), os.path.join(cur_dir, n)
    if not os.path.exists(bp): print(f"NEU (nicht in Baseline): {n}"); total_diff += 1; continue
    if not os.path.exists(cp): print(f"FEHLT im aktuellen Lauf: {n}"); total_diff += 1; continue
    bh, br = load(bp); ch, cr = load(cp)
    diffs = []
    if bh != ch: diffs.append("  Kopfzeile unterschiedlich")
    if br.get("#PLANKOPF") != cr.get("#PLANKOPF"): diffs.append("  #PLANKOPF unterschiedlich")
    keys = [k for k in br if not k.startswith("#")] + [k for k in cr if not k.startswith("#") and k not in br]
    for k in keys:
        if k not in cr: diffs.append(f"  Zeile fehlt: {k}"); continue
        if k not in br: diffs.append(f"  Zeile neu:   {k}"); continue
        b, c = br[k], cr[k]
        for i, col in enumerate(bh):
            bv = b[i] if i < len(b) else ""
            cv = c[i] if i < len(c) else ""
            if bv != cv: diffs.append(f"  {k} / {col}: {bv!r} -> {cv!r}")
    if diffs:
        total_diff += len(diffs)
        print(f"ABWEICHUNG {n}"); print("\n".join(diffs))
    else:
        print(f"identisch  {n}")
print(f"\n{total_diff} Abweichung(en)." if total_diff else "\nAlle Summen-CSVs identisch.")
