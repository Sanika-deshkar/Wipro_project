"""Train and evaluate the vehicle risk classifier.

Pipeline:  load CSV -> validate/clean -> feature selection -> stratified
80/20 split -> train Decision Tree (baseline) and Random Forest -> evaluate on
the held-out test set -> save the best model + reports.

Usage:  python ml/train_model.py            (run from the project root)
Outputs: ml/model/risk_model.joblib
         ml/reports/metrics.json, metrics.md, confusion_matrix.png,
         confusion_matrix.csv, feature_importance.png
"""
import json
import os
import sys

import joblib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
import sklearn
from sklearn.ensemble import RandomForestClassifier
from sklearn.feature_selection import mutual_info_classif
from sklearn.metrics import (accuracy_score, classification_report,
                             confusion_matrix, precision_recall_fscore_support)
from sklearn.model_selection import StratifiedKFold, cross_val_score, train_test_split
from sklearn.tree import DecisionTreeClassifier

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "vehicle_risk_dataset.csv")
MODEL = os.path.join(HERE, "model", "risk_model.joblib")
REPORTS = os.path.join(HERE, "reports")
SEED = 42
FEATURES = ["speed", "temperature", "fuel", "distance", "seatbelt"]
LABELS = ["Low", "Moderate", "High"]
# physically valid sensor ranges (same limits as include/config.h)
RANGES = {"speed": (0, 220), "temperature": (-20, 150), "fuel": (0, 100),
          "distance": (0, 200), "seatbelt": (0, 1)}


def preprocess(df):
    """Return (clean_df, report dict)."""
    rep = {"rows_raw": int(len(df))}
    missing = [c for c in FEATURES + ["risk"] if c not in df.columns]
    if missing:
        sys.exit(f"dataset is missing columns: {missing}")
    df = df[FEATURES + ["risk"]].copy()
    rep["missing_values"] = int(df.isna().sum().sum())
    df = df.dropna()
    df["risk"] = df["risk"].astype(str).str.strip().str.capitalize()
    bad_label = ~df["risk"].isin(LABELS)
    rep["bad_labels"] = int(bad_label.sum())
    df = df[~bad_label]
    oor = pd.Series(False, index=df.index)
    for c, (lo, hi) in RANGES.items():
        oor |= (df[c] < lo) | (df[c] > hi)
    rep["out_of_range_rows"] = int(oor.sum())
    df = df[~oor]
    rep["duplicate_rows_kept"] = int(df.duplicated().sum())   # harmless, kept
    df["seatbelt"] = df["seatbelt"].astype(int)
    rep["rows_clean"] = int(len(df))
    rep["class_counts"] = {k: int(v) for k, v in df["risk"].value_counts().reindex(LABELS).items()}
    return df.reset_index(drop=True), rep


def feature_selection(X, y, rf):
    """Rank features three ways; all five are kept (each is a C-project sensor)."""
    ycode = y.map({l: i for i, l in enumerate(LABELS)})
    mi = mutual_info_classif(X, ycode, random_state=SEED, discrete_features=[False] * 4 + [True])
    corr = X.assign(y=ycode).corr()["y"].drop("y")
    return {
        "mutual_information": dict(zip(FEATURES, map(float, mi))),
        "correlation_with_risk": dict(zip(FEATURES, map(float, corr))),
        "random_forest_importance": dict(zip(FEATURES, map(float, rf.feature_importances_))),
    }


def scores(model, Xte, yte):
    pred = model.predict(Xte)
    p, r, f, _ = precision_recall_fscore_support(yte, pred, labels=LABELS, average="macro", zero_division=0)
    return {"accuracy": float(accuracy_score(yte, pred)), "precision_macro": float(p),
            "recall_macro": float(r), "f1_macro": float(f)}, pred


def main():
    df = pd.read_csv(DATA)
    df, prep = preprocess(df)
    X, y = df[FEATURES], df["risk"]

    Xtr, Xte, ytr, yte = train_test_split(X, y, test_size=0.20, stratify=y, random_state=SEED)
    split = {"train_rows": int(len(Xtr)), "test_rows": int(len(Xte)), "test_size": 0.20,
             "stratified": True, "random_state": SEED}

    dt = DecisionTreeClassifier(max_depth=10, random_state=SEED).fit(Xtr, ytr)
    rf = RandomForestClassifier(n_estimators=200, max_depth=14, min_samples_leaf=2,
                                random_state=SEED, n_jobs=-1).fit(Xtr, ytr)

    dt_s, _ = scores(dt, Xte, yte)
    rf_s, pred = scores(rf, Xte, yte)
    cv = StratifiedKFold(5, shuffle=True, random_state=SEED)
    dt_cv = cross_val_score(DecisionTreeClassifier(max_depth=10, random_state=SEED), Xtr, ytr, cv=cv)
    rf_cv = cross_val_score(RandomForestClassifier(n_estimators=100, max_depth=14, min_samples_leaf=2,
                                                   random_state=SEED, n_jobs=-1), Xtr, ytr, cv=cv)

    cm = confusion_matrix(yte, pred, labels=LABELS)
    report = classification_report(yte, pred, labels=LABELS, output_dict=True, zero_division=0)
    sel = feature_selection(X, y, rf)

    # ---- save model (with the metadata the predictor needs) ----
    os.makedirs(os.path.dirname(MODEL), exist_ok=True)
    joblib.dump({"model": rf, "features": FEATURES, "labels": LABELS, "ranges": RANGES,
                 "sklearn": sklearn.__version__}, MODEL)

    os.makedirs(REPORTS, exist_ok=True)
    out = {"dataset": os.path.relpath(DATA, os.path.dirname(HERE)).replace("\\", "/"),
           "preprocessing": prep, "split": split, "feature_selection": sel,
           "selected_model": "RandomForestClassifier(n_estimators=200, max_depth=14, min_samples_leaf=2)",
           "random_forest": {**rf_s, "cv5_accuracy_mean": float(rf_cv.mean()), "cv5_accuracy_std": float(rf_cv.std())},
           "decision_tree_baseline": {**dt_s, "cv5_accuracy_mean": float(dt_cv.mean()), "cv5_accuracy_std": float(dt_cv.std())},
           "per_class": {l: {k: float(report[l][k]) for k in ("precision", "recall", "f1-score", "support")} for l in LABELS},
           "confusion_matrix": {"labels": LABELS, "rows_true_cols_pred": cm.tolist()},
           "sklearn_version": sklearn.__version__}
    with open(os.path.join(REPORTS, "metrics.json"), "w") as fh:
        json.dump(out, fh, indent=2)
    pd.DataFrame(cm, index=[f"true_{l}" for l in LABELS], columns=[f"pred_{l}" for l in LABELS]).to_csv(
        os.path.join(REPORTS, "confusion_matrix.csv"))

    # ---- plots ----
    fig, ax = plt.subplots(figsize=(5, 4.2))
    ax.imshow(cm, cmap="Blues")
    ax.set_xticks(range(3), LABELS)
    ax.set_yticks(range(3), LABELS)
    ax.set_xlabel("Predicted")
    ax.set_ylabel("Actual")
    ax.set_title("Random Forest - confusion matrix (test set)")
    for i in range(3):
        for j in range(3):
            ax.text(j, i, cm[i, j], ha="center", va="center", color="white" if cm[i, j] > cm.max() / 2 else "black")
    fig.tight_layout()
    fig.savefig(os.path.join(REPORTS, "confusion_matrix.png"), dpi=130)
    plt.close(fig)

    imp = sel["random_forest_importance"]
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    order = sorted(imp, key=imp.get)
    ax.barh(order, [imp[k] for k in order], color="#2b6cb0")
    ax.set_xlabel("Importance")
    ax.set_title("Random Forest feature importance")
    fig.tight_layout()
    fig.savefig(os.path.join(REPORTS, "feature_importance.png"), dpi=130)
    plt.close(fig)

    # ---- markdown summary ----
    L = ["# ML model evaluation", "",
         f"Dataset: `{out['dataset']}` ({prep['rows_clean']} clean rows). "
         f"Split: {split['train_rows']} train / {split['test_rows']} test (stratified, seed {SEED}).", "",
         "| Model | Accuracy | Precision (macro) | Recall (macro) | F1 (macro) | 5-fold CV acc |",
         "|---|---|---|---|---|---|"]
    for n, s, c in (("Decision Tree (baseline)", dt_s, dt_cv), ("**Random Forest (selected)**", rf_s, rf_cv)):
        L.append(f"| {n} | {s['accuracy']:.4f} | {s['precision_macro']:.4f} | {s['recall_macro']:.4f} | "
                 f"{s['f1_macro']:.4f} | {c.mean():.4f} ± {c.std():.4f} |")
    L += ["", "## Per-class results (Random Forest, test set)", "",
          "| Class | Precision | Recall | F1 | Support |", "|---|---|---|---|---|"]
    for l in LABELS:
        r = report[l]
        L.append(f"| {l} | {r['precision']:.4f} | {r['recall']:.4f} | {r['f1-score']:.4f} | {int(r['support'])} |")
    L += ["", "## Confusion matrix (rows = actual, columns = predicted)", "",
          "| | " + " | ".join(f"pred {l}" for l in LABELS) + " |", "|---|---|---|---|"]
    for i, l in enumerate(LABELS):
        L.append(f"| **actual {l}** | " + " | ".join(str(v) for v in cm[i]) + " |")
    L += ["", "## Feature importance (Random Forest)", "",
          "| Feature | Importance | Mutual information | Corr. with risk |", "|---|---|---|---|"]
    for k in FEATURES:
        L.append(f"| {k} | {imp[k]:.3f} | {sel['mutual_information'][k]:.3f} | {sel['correlation_with_risk'][k]:+.3f} |")
    with open(os.path.join(REPORTS, "metrics.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(L) + "\n")

    print("\n".join(L))
    print(f"\npreprocessing: {prep}\nmodel saved -> {MODEL}")


if __name__ == "__main__":
    main()
