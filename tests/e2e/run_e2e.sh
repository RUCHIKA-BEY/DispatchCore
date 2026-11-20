#!/bin/sh
# run_e2e.sh — end-to-end tests of the real program (Phase 9).
#
# Usage (from the project root):   sh tests/e2e/run_e2e.sh build/dispatchcore.exe
#
# For every tests/e2e/NAME.in, runs
#     PROGRAM --data tests/e2e/data --no-autosave --echo < NAME.in > build/e2e/NAME.out
# and checks that every line of NAME.expect appears in the output as a WHOLE
# line (grep -x -F: exact, no regular expressions). --no-autosave guarantees
# that tests/e2e/data is never written.
#
# It also checks two start-up failures: a missing data folder (exit 1) and an
# unknown argument (exit 2).
#
# Exit status: 0 if every check passed, 1 otherwise.

PROGRAM=${1:-build/dispatchcore.exe}
DATA=tests/e2e/data
OUTDIR=build/e2e
mkdir -p "$OUTDIR"

passed=0
failed=0
not_run=0

# Exit status 126 = "found but could not be executed" (e.g. blocked by
# Windows Smart App Control). That is NOT a test failure: nothing ran, so
# nothing can be checked. It is counted separately and reported as NOT RUN.
blocked() {   # blocked EXIT-STATUS NAME
    if [ "$1" -eq 126 ]; then
        not_run=$((not_run + 1))
        echo "  NOT RUN: $2 (program could not be started, exit 126)"
        return 0
    fi
    return 1
}

check() {   # check DESCRIPTION CONDITION-EXIT-STATUS
    if [ "$2" -eq 0 ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "  FAIL: $1"
    fi
}

for script in tests/e2e/*.in; do
    name=$(basename "$script" .in)
    out="$OUTDIR/$name.out"
    echo "===== e2e $name ====="
    "$PROGRAM" --data "$DATA" --no-autosave --echo < "$script" > "$out" 2>&1
    status=$?
    if blocked $status "$name"; then
        continue
    fi
    check "$name: exit status 0" $status
    # read the expectations line by line (IFS= keeps leading spaces)
    while IFS= read -r line || [ -n "$line" ]; do
        grep -qxF -- "$line" "$out"
        check "$name: line not found: $line" $?
    done < "tests/e2e/$name.expect"
done

echo "===== e2e start-up failures ====="
"$PROGRAM" --data tests/e2e/no_such_folder --no-autosave < /dev/null > "$OUTDIR/missing.out" 2>&1
status=$?
if ! blocked $status "missing data folder"; then
    [ $status -eq 1 ]
    check "missing data folder: exit status 1" $?
    grep -qF "Could not load the data" "$OUTDIR/missing.out"
    check "missing data folder: message" $?
fi

"$PROGRAM" --bogus < /dev/null > "$OUTDIR/bad_arg.out" 2>&1
status=$?
if ! blocked $status "unknown argument"; then
    [ $status -eq 2 ]
    check "unknown argument: exit status 2" $?
fi

echo
echo "$passed e2e checks passed, $failed failed, $not_run runs NOT RUN (blocked)"
[ "$failed" -eq 0 ] && [ "$not_run" -eq 0 ]
