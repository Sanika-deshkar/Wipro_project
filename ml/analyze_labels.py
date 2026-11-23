"""Explain how the dataset's risk labels relate to the C project's safety thresholds.

The CSV does not ship with its labelling script, so this script documents the
labels empirically: it counts, for every row, how many severity points the
rule-based thresholds of include/config.h would award (Warning = 1, Critical = 2,
per sensor, seatbelt rule included) and cross-tabulates that against the label.

Usage: python ml/analyze_labels.py          -> ml/reports/label_analysis.md
"""
import os

import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.read_csv(os.path.join(HERE, "data", "vehicle_risk_dataset.csv"))


def level(x, warn, crit, high_bad=True):
    if high_bad:
        return (x > crit) * 2 + ((x > warn) & (x <= crit)) * 1
    return (x < crit) * 2 + ((x < warn) & (x >= crit)) * 1


pts = (level(df.speed, 80, 120) + level(df.temperature, 100, 110)
       + level(df.fuel, 15, 5, False) + level(df.distance, 10, 3, False))
unbelted = (df.seatbelt == 0) & (df.speed > 0)
pts = pts + np.where(unbelted, np.where(df.speed > 40, 2, 1), 0)
pts.name = "severity_points"

def md(frame, index_name=""):
    """Minimal DataFrame -> markdown table (avoids the optional 'tabulate' dependency)."""
    head = "| " + index_name + " | " + " | ".join(map(str, frame.columns)) + " |"
    rows = ["| " + str(i) + " | " + " | ".join(str(v) for v in r) + " |" for i, r in zip(frame.index, frame.values)]
    return chr(10).join([head, "|" + "---|" * (len(frame.columns) + 1)] + rows)


tab = pd.crosstab(pts, df.risk)[["Low", "Moderate", "High"]]
share = tab.div(tab.sum(axis=1), axis=0).round(3)

tree = DecisionTreeClassifier(max_depth=3, random_state=0).fit(df.drop(columns="risk"), df.risk)
rules = export_text(tree, feature_names=list(df.drop(columns="risk").columns))

out = ["# Label analysis", "",
       f"Rows: {len(df)}. Features: " + ", ".join(df.columns[:-1]) + ".", "",
       "## Ranges", "", md(df.describe().loc[["min", "max", "mean"]].round(2).T, "feature"), "",
       "## Severity points (rule thresholds of config.h) vs. label", "",
       "Points = sum over speed, temperature, fuel, obstacle and seatbelt of Warning(1) / Critical(2).", "",
       md(tab, "points"), "", "Row-normalised:", "", md(share, "points"), "",
       "## Depth-3 decision tree on the raw features (shows the dominant rules)", "", "```", rules, "```"]
path = os.path.join(HERE, "reports", "label_analysis.md")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(out) + "\n")
print("\n".join(out))
