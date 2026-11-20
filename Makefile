# DispatchCore Makefile (GNU make)
#
# Run from a POSIX-style shell such as MSYS2 UCRT64 or Git Bash.
# (Plain cmd.exe lacks `mkdir -p` and `rm -rf`; the phase documents list the
#  equivalent gcc commands without make.)
#
#   make          build the program (build/dispatchcore) and all test programs
#   make test     build and run all unit tests
#   make e2e      run the real program on the scripts in tests/e2e/
#   make run      start the program on the live data/ folder
#   make bench    timing measurements (Phase 10)
#   make analyze  compile with extra warnings + GCC static analyzer (no linking)
#   make clean    delete the build/ folder

CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests
BUILD  = build

# Stricter flags for `make analyze`. -O2 enables data-flow warnings such as
# -Wmaybe-uninitialized; -fanalyzer looks for leaks, double frees, NULL
# dereferences and use-after-free along every code path.
ANALYZE_FLAGS = $(CFLAGS) -O2 -Wshadow -Wformat=2 -Wnull-dereference \
                -Wstrict-prototypes -Wmissing-prototypes -fanalyzer

ifeq ($(OS),Windows_NT)
  EXE = .exe
else
  EXE =
endif

# ---- module groups (extended in later phases) ----
CORE_SRC     = src/common.c
GRAPH_SRC    = src/graph.c
MINHEAP_SRC  = src/minheap.c
DIJKSTRA_SRC = src/dijkstra.c
INCIDENT_SRC = src/incident.c
IHEAP_SRC    = src/incident_heap.c
RESOURCE_SRC = src/resource.c
COMPAT_SRC   = src/compatibility.c
HASH_SRC     = src/hash_table.c
APP_SRC      = src/app_state.c
HISTORY_SRC  = src/history.c
DISPATCH_SRC = src/dispatch.c
FM_SRC       = src/file_manager.c
REPORT_SRC   = src/report.c
INPUT_SRC    = src/input.c
MENU_SRC     = src/menu.c
EVAL_SRC     = src/evaluation.c
MAIN_SRC     = src/main.c
FIXTURES     = tests/reference_city.c

# Every module except main.c (tests have their own main()).
ALL_SRC = $(CORE_SRC) $(GRAPH_SRC) $(MINHEAP_SRC) $(DIJKSTRA_SRC) \
          $(INCIDENT_SRC) $(IHEAP_SRC) $(RESOURCE_SRC) $(COMPAT_SRC) \
          $(HASH_SRC) $(APP_SRC) $(HISTORY_SRC) $(DISPATCH_SRC) $(FM_SRC) \
          $(REPORT_SRC) $(INPUT_SRC) $(MENU_SRC) $(EVAL_SRC)

# ---- the program ----
APP_EXE = $(BUILD)/dispatchcore$(EXE)
HEADERS = $(wildcard src/*.h) $(wildcard tests/*.h)

# ---- test programs ----
TEST_GRAPH    = $(BUILD)/test_graph$(EXE)
TEST_MINHEAP  = $(BUILD)/test_minheap$(EXE)
TEST_DIJKSTRA = $(BUILD)/test_dijkstra$(EXE)
TEST_INCIDENT = $(BUILD)/test_incident$(EXE)
TEST_IHEAP    = $(BUILD)/test_incident_heap$(EXE)
TEST_RESOURCE = $(BUILD)/test_resource$(EXE)
TEST_COMPAT   = $(BUILD)/test_compatibility$(EXE)
TEST_HASH     = $(BUILD)/test_hash_table$(EXE)
TEST_APP      = $(BUILD)/test_app_state$(EXE)
TEST_HISTORY  = $(BUILD)/test_history$(EXE)
TEST_DISPATCH = $(BUILD)/test_dispatch$(EXE)
TEST_FM       = $(BUILD)/test_file_manager$(EXE)
TEST_REPORT   = $(BUILD)/test_report$(EXE)
TEST_INPUT    = $(BUILD)/test_input$(EXE)
TEST_MENU     = $(BUILD)/test_menu$(EXE)
TEST_EVAL     = $(BUILD)/test_evaluation$(EXE)
TESTS         = $(TEST_GRAPH) $(TEST_MINHEAP) $(TEST_DIJKSTRA) \
                $(TEST_INCIDENT) $(TEST_IHEAP) $(TEST_RESOURCE) $(TEST_COMPAT) \
                $(TEST_HASH) $(TEST_APP) $(TEST_HISTORY) $(TEST_DISPATCH) $(TEST_FM) \
                $(TEST_REPORT) $(TEST_INPUT) $(TEST_MENU) $(TEST_EVAL)
TEST_SOURCES  = tests/test_graph.c tests/test_minheap.c tests/test_dijkstra.c \
                tests/test_incident.c tests/test_incident_heap.c \
                tests/test_resource.c tests/test_compatibility.c \
                tests/test_hash_table.c tests/test_app_state.c \
                tests/test_history.c tests/test_dispatch.c tests/test_file_manager.c \
                tests/test_report.c tests/test_input.c tests/test_menu.c \
                tests/test_evaluation.c

# Everything AppState needs (graph, stores, heap, indexes, history).
APP_DEPS = $(APP_SRC) $(HASH_SRC) $(GRAPH_SRC) $(INCIDENT_SRC) $(IHEAP_SRC) \
           $(RESOURCE_SRC) $(HISTORY_SRC) $(CORE_SRC)

.PHONY: all test e2e run bench analyze clean

all: $(APP_EXE) $(TESTS)

$(APP_EXE): $(MAIN_SRC) $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRC) $(ALL_SRC)

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_GRAPH): tests/test_graph.c $(FIXTURES) $(GRAPH_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_graph.c $(FIXTURES) $(GRAPH_SRC) $(CORE_SRC)

$(TEST_MINHEAP): tests/test_minheap.c $(MINHEAP_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_minheap.c $(MINHEAP_SRC) $(CORE_SRC)

$(TEST_DIJKSTRA): tests/test_dijkstra.c $(FIXTURES) $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_dijkstra.c $(FIXTURES) $(CORE_SRC) $(GRAPH_SRC) $(MINHEAP_SRC) $(DIJKSTRA_SRC)

$(TEST_INCIDENT): tests/test_incident.c $(INCIDENT_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_incident.c $(INCIDENT_SRC) $(CORE_SRC)

$(TEST_IHEAP): tests/test_incident_heap.c $(IHEAP_SRC) $(INCIDENT_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_incident_heap.c $(IHEAP_SRC) $(INCIDENT_SRC) $(CORE_SRC)

$(TEST_RESOURCE): tests/test_resource.c $(RESOURCE_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_resource.c $(RESOURCE_SRC) $(CORE_SRC)

$(TEST_COMPAT): tests/test_compatibility.c $(COMPAT_SRC) $(RESOURCE_SRC) $(INCIDENT_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_compatibility.c $(COMPAT_SRC) $(RESOURCE_SRC) $(INCIDENT_SRC) $(CORE_SRC)

$(TEST_HASH): tests/test_hash_table.c $(HASH_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_hash_table.c $(HASH_SRC) $(CORE_SRC)

$(TEST_APP): tests/test_app_state.c $(FIXTURES) $(APP_DEPS) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_app_state.c $(FIXTURES) $(APP_DEPS)

$(TEST_HISTORY): tests/test_history.c $(HISTORY_SRC) $(CORE_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_history.c $(HISTORY_SRC) $(CORE_SRC)

$(TEST_DISPATCH): tests/test_dispatch.c $(FIXTURES) $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_dispatch.c $(FIXTURES) $(ALL_SRC)

$(TEST_FM): tests/test_file_manager.c $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_file_manager.c $(ALL_SRC)

$(TEST_REPORT): tests/test_report.c $(FIXTURES) $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_report.c $(FIXTURES) $(ALL_SRC)

$(TEST_INPUT): tests/test_input.c $(INPUT_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_input.c $(INPUT_SRC)

$(TEST_MENU): tests/test_menu.c $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_menu.c $(ALL_SRC)

$(TEST_EVAL): tests/test_evaluation.c $(FIXTURES) $(ALL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_evaluation.c $(FIXTURES) $(ALL_SRC)

# Runs every test even if an earlier one fails, then reports overall status.
# Test output files go under build/test_data only:
#   roundtrip/  file-manager test        report.txt  report test
#   input_*.txt input test               menu/       menu autosave test, recreated
#                                                    from tests/e2e/data on every run
test: $(TESTS)
	@mkdir -p $(BUILD)/test_data/roundtrip
	@rm -rf $(BUILD)/test_data/menu
	@mkdir -p $(BUILD)/test_data/menu
	@cp tests/e2e/data/locations.csv tests/e2e/data/roads.csv $(BUILD)/test_data/menu/
	@status=0; \
	for t in $(TESTS); do \
	    echo "===== $$t ====="; \
	    ./$$t || status=1; \
	done; \
	exit $$status

# Compiles (-c) every file to a throw-away object file. NOTE: -fsyntax-only
# must NOT be used here — it stops after parsing, so neither -fanalyzer nor
# the -O2 data-flow warnings would run (verified on 2026-11-19).
# -Werror makes any warning fail the target, so a clean exit means clean code.
ANALYZE_FILES = $(ALL_SRC) $(MAIN_SRC) $(FIXTURES) $(TEST_SOURCES)

# End-to-end tests of the real program (never writes tests/e2e/data).
e2e: $(APP_EXE)
	sh tests/e2e/run_e2e.sh ./$(APP_EXE)

# Interactive run on the live data folder (autosave ON: writes data/*.csv).
run: $(APP_EXE)
	./$(APP_EXE) --data data

# Timing measurements (Phase 10). Machine-dependent; reads and writes no data.
bench: $(APP_EXE)
	./$(APP_EXE) --benchmark

analyze: | $(BUILD)
	mkdir -p $(BUILD)/analyze
	@status=0; \
	for f in $(ANALYZE_FILES); do \
	    echo "analyze $$f"; \
	    $(CC) $(ANALYZE_FLAGS) -Werror -c $$f -o $(BUILD)/analyze/$$(basename $$f .c).o || status=1; \
	done; \
	exit $$status

clean:
	rm -rf $(BUILD)
