/**
 * @file    ml.h
 * @brief   Bridge to the Python / scikit-learn risk-prediction model.
 *
 * This module is deliberately INDEPENDENT of the rule engine (monitor.c):
 * it never reads or changes a SafetyReport, and the rule-based alerts never
 * depend on it.  If Python, scikit-learn or the model file is missing the
 * rest of the application works exactly as before and the dashboard simply
 * shows "ML: unavailable".
 *
 * Mechanism: ml_start() launches `python ml/predict.py --serve` as a child
 * process with two pipes (stdin/stdout).  Each prediction is one text line
 * out ("speed temp fuel distance belt") and one text line back
 * ("OK High 0.97 0.01 0.02 0.97").  The model is loaded once.
 */
#ifndef ML_H
#define ML_H

#include <stdbool.h>
#include "vehicle.h"

typedef enum { ML_LOW = 0, ML_MODERATE = 1, ML_HIGH = 2 } MlLevel;

typedef struct {
    bool    valid;          /**< false -> no usable prediction            */
    MlLevel level;          /**< predicted class                          */
    char    label[12];      /**< "Low" / "Moderate" / "High"              */
    float   confidence;     /**< probability of the predicted class 0..1  */
    float   probs[3];       /**< P(Low), P(Moderate), P(High)             */
} MlPrediction;

/**
 * Start the predictor.
 * @param python_cmd  interpreter (NULL -> $SVSM_PYTHON, else python3 / python)
 * @param script      path to predict.py (NULL -> $SVSM_ML_SCRIPT, else ml/predict.py)
 * @return 0 on success; -1 on failure (reason in ml_last_error())
 */
int  ml_start(const char *python_cmd, const char *script);

/** Stop the predictor (safe to call any time, also if never started). */
void ml_stop(void);

/** true while the predictor process is running and healthy. */
bool ml_available(void);

/** Human-readable reason for the last failure ("" if none). */
const char *ml_last_error(void);

/**
 * Predict the risk class for the current vehicle readings.
 * Invalid readings (NaN, out of range) or a dead predictor give
 * out->valid == false and a false return; nothing else is affected.
 */
bool ml_predict(const Vehicle *v, MlPrediction *out);

/* ---- pure helpers (no process needed; used by the unit tests) -------- */

/** Build the request line; returns false if any reading is invalid. */
bool ml_format_request(const Vehicle *v, char *buf, int size);

/** Parse a reply line ("OK ..." / "ERR ..."); returns true only for a valid OK. */
bool ml_parse_reply(const char *line, MlPrediction *out);

const char *ml_level_name(MlLevel l);

#endif /* ML_H */
