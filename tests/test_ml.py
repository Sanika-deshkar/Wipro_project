"""Tests for the ML risk predictor (ml/predict.py) and the trained model.

Run with the standard library runner (NOT pytest):
    python -m unittest discover -s tests -p "test_ml*.py" -v      (or: make ml-test)
"""
import json
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "ml"))

import predict  # noqa: E402

PREDICT = os.path.join(ROOT, "ml", "predict.py")
METRICS = os.path.join(ROOT, "ml", "reports", "metrics.json")


class ModelCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = predict.load()

    def risk(self, speed, temp, fuel, dist, belt):
        vals = predict.parse([str(speed), str(temp), str(fuel), str(dist), str(belt)], self.bundle)
        return predict.predict(self.bundle, vals)

    # ---- different input scenarios ---------------------------------
    def test_safe_scenario_is_low(self):
        self.assertEqual(self.risk(50, 85, 70, 40, 1)[0], "Low")

    def test_parked_scenario_is_low(self):
        self.assertEqual(self.risk(0, 70, 60, 30, 1)[0], "Low")

    def test_moderate_scenario(self):
        self.assertEqual(self.risk(95, 85, 60, 30, 1)[0], "Moderate")

    def test_low_fuel_alone_is_moderate(self):
        self.assertEqual(self.risk(50, 85, 3, 40, 1)[0], "Moderate")

    def test_high_risk_overspeed_no_belt(self):
        self.assertEqual(self.risk(150, 85, 60, 40, 0)[0], "High")

    def test_high_risk_everything_wrong(self):
        self.assertEqual(self.risk(150, 113, 3, 2, 0)[0], "High")

    def test_high_risk_overheat_and_close_obstacle(self):
        self.assertEqual(self.risk(70, 115, 60, 2, 1)[0], "High")

    def test_belt_matters(self):
        belted, unbelted = self.risk(60, 85, 60, 40, 1)[0], self.risk(60, 85, 60, 40, 0)[0]
        self.assertNotEqual(belted, unbelted)

    def test_probabilities_sum_to_one(self):
        _, conf, probs = self.risk(100, 95, 30, 20, 1)
        self.assertAlmostEqual(sum(probs), 1.0, places=3)
        self.assertAlmostEqual(conf, max(probs), places=6)

    def test_boundary_values_accepted(self):
        self.assertIn(self.risk(0, -20, 0, 0, 0)[0], predict.load()["labels"])
        self.assertIn(self.risk(220, 150, 100, 200, 1)[0], predict.load()["labels"])

    def test_reply_format(self):
        self.assertRegex(predict.fmt(*self.risk(50, 85, 70, 40, 1)),
                         r"^OK (Low|Moderate|High) [01]\.\d{3}( [01]\.\d{3}){3}$")


class InvalidInputCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = predict.load()

    def bad(self, tokens, fragment):
        with self.assertRaises(ValueError) as cm:
            predict.parse(tokens, self.bundle)
        self.assertIn(fragment, str(cm.exception))

    def test_too_few_values(self):
        self.bad(["1", "2"], "expected 5")

    def test_too_many_values(self):
        self.bad(["1"] * 6, "expected 5")

    def test_not_a_number(self):
        self.bad(["fast", "80", "50", "20", "1"], "not a number")

    def test_nan(self):
        self.bad(["nan", "80", "50", "20", "1"], "not finite")

    def test_inf(self):
        self.bad(["inf", "80", "50", "20", "1"], "not finite")

    def test_speed_out_of_range(self):
        self.bad(["300", "80", "50", "20", "1"], "outside valid range")

    def test_negative_fuel(self):
        self.bad(["50", "80", "-5", "20", "1"], "outside valid range")

    def test_obstacle_too_far(self):
        self.bad(["50", "80", "50", "500", "1"], "outside valid range")

    def test_seatbelt_must_be_binary(self):
        self.bad(["50", "80", "50", "20", "0.5"], "must be 0 or 1")


class ServiceCase(unittest.TestCase):
    """The long-running mode the C program talks to."""

    def run_service(self, text):
        p = subprocess.run([sys.executable, PREDICT, "--serve"], input=text, capture_output=True,
                           text=True, timeout=60)
        return p.returncode, p.stdout.strip().splitlines()

    def test_service_protocol_and_survives_bad_lines(self):
        rc, out = self.run_service("50 85 70 40 1\nbanana\n1 2\n300 85 70 40 1\n150 113 3 2 0\nquit\n")
        self.assertEqual(rc, 0)
        self.assertEqual(out[0], "READY")
        self.assertTrue(out[1].startswith("OK Low"))
        self.assertTrue(out[2].startswith("ERR"))
        self.assertTrue(out[3].startswith("ERR"))
        self.assertTrue(out[4].startswith("ERR"))
        self.assertTrue(out[5].startswith("OK High"))     # still alive after 3 bad requests

    def test_service_exits_on_eof(self):
        rc, out = self.run_service("")
        self.assertEqual((rc, out), (0, ["READY"]))

    def test_missing_model_is_reported(self):
        env = dict(os.environ, SVSM_MODEL=os.path.join(ROOT, "no_such_model.joblib"))
        p = subprocess.run([sys.executable, PREDICT, "--serve"], input="", capture_output=True,
                           text=True, env=env, timeout=60)
        self.assertEqual(p.returncode, 3)
        self.assertTrue(p.stdout.startswith("ERR cannot load model"))

    def test_one_shot_cli(self):
        p = subprocess.run([sys.executable, PREDICT, "150", "113", "3", "2", "0"], capture_output=True,
                           text=True, timeout=60)
        self.assertEqual(p.returncode, 0)
        self.assertTrue(p.stdout.startswith("OK High"))
        p = subprocess.run([sys.executable, PREDICT, "x"], capture_output=True, text=True, timeout=60)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(p.stdout.startswith("ERR"))


class MetricsCase(unittest.TestCase):
    def test_reported_metrics_meet_target(self):
        with open(METRICS) as fh:
            m = json.load(fh)
        for k in ("accuracy", "precision_macro", "recall_macro", "f1_macro"):
            self.assertGreaterEqual(m["random_forest"][k], 0.95, k)
        cm = m["confusion_matrix"]["rows_true_cols_pred"]
        self.assertEqual(sum(map(sum, cm)), m["split"]["test_rows"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
