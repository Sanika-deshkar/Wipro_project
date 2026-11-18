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

SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(SRCS:$(SRC_DIR)/%.c=$(BUILD)/%.o)
DEPS     := $(OBJS:.o=.d)

# Everything except main.c is shared with the unit-test binary.
LIB_OBJS  := $(filter-out $(BUILD)/main.o,$(OBJS))
TEST_BIN  := $(BUILD)/test_monitor

.PHONY: all start run demo serve test test-api debug history clean distclean help

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
	@sh start.sh $(BUILD)/$(TARGET)

serve: all
	@./$(BUILD)/$(TARGET) --serve 8080

test-api: all
	@sh $(TEST_DIR)/test_api.sh $(BUILD)/$(TARGET)

history: all
	@./$(BUILD)/$(TARGET) --history 20

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
	@echo "  debug          rebuild with debug symbols + sanitizers"
	@echo "  clean          remove build/"
	@echo "  distclean      remove build/ and logs/"

-include $(DEPS)
