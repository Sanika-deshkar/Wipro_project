# ML model evaluation

Dataset: `ml/data/vehicle_risk_dataset.csv` (10000 clean rows). Split: 8000 train / 2000 test (stratified, seed 42).

| Model | Accuracy | Precision (macro) | Recall (macro) | F1 (macro) | 5-fold CV acc |
|---|---|---|---|---|---|
| Decision Tree (baseline) | 0.9960 | 0.9966 | 0.9971 | 0.9968 | 0.9980 ± 0.0009 |
| **Random Forest (selected)** | 0.9960 | 0.9965 | 0.9972 | 0.9968 | 0.9970 ± 0.0012 |

## Per-class results (Random Forest, test set)

| Class | Precision | Recall | F1 | Support |
|---|---|---|---|---|
| Low | 1.0000 | 1.0000 | 1.0000 | 281 |
| Moderate | 0.9906 | 0.9986 | 0.9946 | 739 |
| High | 0.9990 | 0.9929 | 0.9959 | 980 |

## Confusion matrix (rows = actual, columns = predicted)

| | pred Low | pred Moderate | pred High |
|---|---|---|---|
| **actual Low** | 281 | 0 | 0 |
| **actual Moderate** | 0 | 738 | 1 |
| **actual High** | 0 | 7 | 973 |

## Feature importance (Random Forest)

| Feature | Importance | Mutual information | Corr. with risk |
|---|---|---|---|
| speed | 0.348 | 0.191 | +0.528 |
| temperature | 0.264 | 0.124 | +0.390 |
| fuel | 0.043 | 0.016 | -0.063 |
| distance | 0.128 | 0.046 | -0.181 |
| seatbelt | 0.217 | 0.108 | -0.432 |
