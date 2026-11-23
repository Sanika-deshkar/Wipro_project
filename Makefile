# ============================================================================
#  Smart Vehicle Safety Monitoring and Dashboard System  -  Makefile
#  Target platform : Linux (GCC, GNU Make)
#
#  Common usage
#     make            build the application   -> build/svsm
#     make run        build and start the interactive dashboard
#     make demo       build and run a 40-second scripted scenario
#     make start      ONE command: build + start the C backend + open the browser
#     make serve      start the C backend + web dashboard (http://localhost:8080)
#     make test       build and run the unit tests
#     make test-api   start the server and test every API endpoint with curl
#     make debug      clean rebuild + tests with -g -O0 and sanitizers
#     make clean      remove build artefacts
#     make help       list all targets
# ============================================================================

CC       ?= gcc
TARGET   := svsm
BUILD    := build
SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests

CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -I$(INC_DIR) -D_POSIX_C_SOURCE=200809L
DEPFLAGS  = -MMD -MP
LDFLAGS  :=

# SAN=1 -> debug build with AddressSanitizer + UBSan (used by `make debug`)
ifdef SAN
CFLAGS  += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
else
CFLAGS  += -O2
endif

# building natively on Windows (MinGW / MSYS): sockets + shell libraries
ifeq ($(OS),Windows_NT)
LDFLAGS += -lws2_32 -lshell32
endif

SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(SRCS:$(SRC_DIR)/%.c=$(BUILD)/%.o)
DEPS     := $(OBJS:.o=.d)

# Everything except main.c is shared with the unit-test binary.
LIB_OBJS  := $(filter-out $(BUILD)/main.o,$(OBJS))
TEST_BIN  := $(BUILD)/test_monitor

.DEFAULT_GOAL := all
.PHONY: ml-setup ml-train ml-test test-ml test-all linux-demo all start run demo serve test test-api debug history clean distclean help dist dist-windows dist-linux

# ready-to-run programs for laptops without a compiler (see dist/)
DIST     := dist
WINCC    ?= x86_64-w64-mingw32-gcc      # apt install gcc-mingw-w64-x86-64

dist: dist-windows dist-linux

dist-windows:
	@mkdir -p $(DIST)
	@$(WINCC) -std=gnu11 -O2 -Wall -Wextra -I$(INC_DIR) -D__USE_MINGW_ANSI_STDIO=1 $(SRCS) \
	    -o $(DIST)/svsm.exe -static -s -lws2_32 -lshell32
	@echo "Built $(DIST)/svsm.exe  (Windows, no install needed)"

dist-linux:
	@mkdir -p $(DIST)
	@$(CC) -std=c11 -O2 -Wall -Wextra -I$(INC_DIR) -D_POSIX_C_SOURCE=200809L $(SRCS) \
	    -o $(DIST)/svsm-linux-x64 -static -s
	@echo "Built $(DIST)/svsm-linux-x64  (Linux x86-64, no install needed)"

all: $(BUILD)/$(TARGET)
	@echo "Build complete -> $(BUILD)/$(TARGET)"

$(BUILD)/$(TARGET): $(OBJS)
	@echo "  LINK  $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

run: all
	@./$(BUILD)/$(TARGET)

demo: all
	@./$(BUILD)/$(TARGET) --demo 40 --seed 7

start: all
	@./$(BUILD)/$(TARGET) --serve 8080 --open

serve: all
	@./$(BUILD)/$(TARGET) --serve 8080

test-api: all
	@sh $(TEST_DIR)/test_api.sh $(BUILD)/$(TARGET)

history: all
	@./$(BUILD)/$(TARGET) --history 20

# ---- ML module (Python / scikit-learn) --------------------------------
PYTHON ?= python3

ml-setup:
	$(PYTHON) -m pip install -r ml/requirements.txt

ml-train:
	$(PYTHON) ml/train_model.py

ml-test:
	$(PYTHON) -m unittest discover -s tests -p "test_ml*.py" -v

test-ml: all
	@sh $(TEST_DIR)/test_ml_integration.sh $(BUILD)/$(TARGET)

test-all: test ml-test test-ml test-api

linux-demo: all
	@sh scripts/linux_demo.sh

test: $(TEST_BIN)
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_DIR)/test_monitor.c $(LIB_OBJS) | $(BUILD)
	@echo "  LINK  $@"
	@$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

debug: clean
	@$(MAKE) --no-print-directory all test SAN=1

clean:
	@rm -rf $(BUILD)
	@echo "Cleaned build directory."

# also deletes the saved alert history
distclean: clean
	@rm -rf logs
	@echo "Removed logs."

help:
	@echo "Targets:"
	@echo "  all (default)  compile the application"
	@echo "  run            compile and start the interactive dashboard"
	@echo "  demo           run a scripted 40 s scenario (no keyboard needed)"
	@echo "  start          build + start the C backend + open the browser (one command)"
	@echo "  serve          C backend + web dashboard at http://localhost:8080"
	@echo "  test-api       start the server and test the API with curl"
	@echo "  history        print the last 20 alert-history records"
	@echo "  test           compile and run unit tests"
	@echo "  ml-setup       pip install the Python ML dependencies"
	@echo "  ml-train       train + evaluate the ML model (ml/reports/)"
	@echo "  ml-test        Python tests of the predictor"
	@echo "  test-ml        C <-> Python integration tests (CLI)"
	@echo "  test-all       every test suite"
	@echo "  linux-demo     scripted Linux demonstration (gcc, make, shell, logs, ML)"
	@echo "  debug          rebuild with debug symbols + sanitizers"
	@echo "  clean          remove build/"
	@echo "  distclean      remove build/ and logs/"

-include $(DEPS)
