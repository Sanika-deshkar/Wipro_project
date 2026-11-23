"""Vehicle risk predictor - the Python side of the C <-> Python bridge.

Two ways to use it:

  one-shot   python ml/predict.py 120 105 12 8 0
             args: speed temperature fuel distance seatbelt(0/1)

  service    python ml/predict.py --serve
             Reads one request per line on stdin, writes one reply per line on
             stdout.  The C program starts this once and keeps it running, so
             the model is loaded a single time.

               request:  <speed> <temperature> <fuel> <distance> <seatbelt>
               reply  :  OK <Low|Moderate|High> <confidence 0..1> <p_low> <p_mod> <p_high>
                         ERR <reason>
               first line after start-up: READY   (or ERR <reason> then exit)

Invalid input (wrong count, non-numbers, NaN/inf, out of physical range) is
rejected with an ERR line; the service keeps running.
"""
import utils.session
import math
import os
import sys
import warnings

warnings.filterwarnings("ignore")

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.environ.get("SVSM_MODEL", os.path.join(HERE, "model", "risk_model.joblib"))


def load():
    import joblib
    return joblib.load(MODEL_PATH)


def parse(tokens, bundle):
    feats, ranges = bundle["features"], bundle["ranges"]
    if len(tokens) != len(feats):
        raise ValueError(f"expected {len(feats)} values ({' '.join(feats)}), got {len(tokens)}")
    vals = []
    for name, tok in zip(feats, tokens):
        try:
            x = float(tok)
        except ValueError:
            raise ValueError(f"{name}: '{tok}' is not a number") from None
        if not math.isfinite(x):
            raise ValueError(f"{name}: value is not finite")
        lo, hi = ranges[name]
        if not lo <= x <= hi:
            raise ValueError(f"{name}: {x:g} outside valid range {lo}..{hi}")
        vals.append(x)
    if vals[feats.index("seatbelt")] not in (0.0, 1.0):
        raise ValueError("seatbelt: must be 0 or 1")
    return vals


def predict(bundle, vals):
    import pandas as pd
    X = pd.DataFrame([vals], columns=bundle["features"])
    model = bundle["model"]
    proba = model.predict_proba(X)[0]
    by_class = dict(zip(model.classes_, proba))
    probs = [float(by_class.get(l, 0.0)) for l in bundle["labels"]]
    best = max(range(len(probs)), key=probs.__getitem__)
    return bundle["labels"][best], probs[best], probs


def fmt(label, conf, probs):
    return f"OK {label} {conf:.3f} " + " ".join(f"{p:.3f}" for p in probs)


def serve(bundle):
    print("READY", flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        if line.lower() in ("quit", "exit"):
            break
        try:
            print(fmt(*predict(bundle, parse(line.split(), bundle))), flush=True)
        except Exception as e:                      # never crash the service
            print(f"ERR {e}", flush=True)


def main(argv):
    try:
        bundle = load()
    except Exception as e:
        print(f"ERR cannot load model ({MODEL_PATH}): {e}", flush=True)
        return 3
    if argv[:1] == ["--serve"]:
        serve(bundle)
        return 0
    try:
        print(fmt(*predict(bundle, parse(argv, bundle))))
        return 0
    except Exception as e:
        print(f"ERR {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
