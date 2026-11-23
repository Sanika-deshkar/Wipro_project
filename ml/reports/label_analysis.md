# Label analysis

Rows: 10000. Features: speed, temperature, fuel, distance, seatbelt.

## Ranges

| feature | min | max | mean |
|---|---|---|---|
| speed | 0.0 | 160.0 | 80.59 |
| temperature | 70.0 | 125.0 | 97.41 |
| fuel | 0.0 | 100.0 | 49.94 |
| distance | 0.5 | 50.0 | 25.5 |
| seatbelt | 0.0 | 1.0 | 0.51 |

## Severity points (rule thresholds of config.h) vs. label

Points = sum over speed, temperature, fuel, obstacle and seatbelt of Warning(1) / Critical(2).

| points | Low | Moderate | High |
|---|---|---|---|
| 0 | 1009 | 0 | 0 |
| 1 | 366 | 1234 | 0 |
| 2 | 27 | 2047 | 108 |
| 3 | 0 | 394 | 1495 |
| 4 | 0 | 21 | 1701 |
| 5 | 0 | 0 | 864 |
| 6 | 0 | 0 | 562 |
| 7 | 0 | 0 | 135 |
| 8 | 0 | 0 | 30 |
| 9 | 0 | 0 | 7 |

Row-normalised:

| points | Low | Moderate | High |
|---|---|---|---|
| 0 | 1.0 | 0.0 | 0.0 |
| 1 | 0.229 | 0.771 | 0.0 |
| 2 | 0.012 | 0.938 | 0.049 |
| 3 | 0.0 | 0.209 | 0.791 |
| 4 | 0.0 | 0.012 | 0.988 |
| 5 | 0.0 | 0.0 | 1.0 |
| 6 | 0.0 | 0.0 | 1.0 |
| 7 | 0.0 | 0.0 | 1.0 |
| 8 | 0.0 | 0.0 | 1.0 |
| 9 | 0.0 | 0.0 | 1.0 |

## Depth-3 decision tree on the raw features (shows the dominant rules)

```
|--- speed <= 80.50
|   |--- temperature <= 100.50
|   |   |--- seatbelt <= 0.50
|   |   |   |--- class: Moderate
|   |   |--- seatbelt >  0.50
|   |   |   |--- class: Low
|   |--- temperature >  100.50
|   |   |--- seatbelt <= 0.50
|   |   |   |--- class: High
|   |   |--- seatbelt >  0.50
|   |   |   |--- class: Moderate
|--- speed >  80.50
|   |--- seatbelt <= 0.50
|   |   |--- class: High
|   |--- seatbelt >  0.50
|   |   |--- temperature <= 100.50
|   |   |   |--- class: Moderate
|   |   |--- temperature >  100.50
|   |   |   |--- class: High

```
