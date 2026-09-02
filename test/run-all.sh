#!/bin/bash
#
# Run every NanoOs test layer and summarise.  Intended for CI and for a
# quick local "is anything broken" check.
#
#   Layer 1  unit          plain tests, no kernel            (make -C test)
#   Layer 2  kernel         real kernel boot under mock HAL  (make -C test)
#   Layer 1  unit + ASan    same plain tests, sanitized      (make -C test ASAN=1)
#   Layer 3  e2e            real simulator over a PTY        (test/e2e/run.py)
#
# Exit status is non-zero if any layer reports a failure.  Known-failing
# tests that document open bugs (see test/BUGS-*.txt) are expected to be red
# until those bugs are fixed.
#
#   test/run-all.sh [--no-e2e] [--no-asan]
#
set -u

cd "$(dirname "${0}")/.."   # repo root

RUN_E2E=1
RUN_ASAN=1
for arg in "$@"; do
	case "${arg}" in
		--no-e2e)  RUN_E2E=0 ;;
		--no-asan) RUN_ASAN=0 ;;
		*) echo "unknown option: ${arg}" >&2; exit 2 ;;
	esac
done

rc=0
section() { printf '\n============ %s ============\n' "$1"; }

section "Layer 1 + 2: kernel test harness"
make -C test clean >/dev/null
if make -C test run; then
	echo "[harness] PASS"
else
	echo "[harness] FAIL"
	rc=1
fi

if [ "${RUN_ASAN}" = "1" ]; then
	section "Layer 1: unit tests under ASan/UBSan"
	echo "(kernel tests are omitted here: halPosixImplInit's heap-sizing"
	echo " stack recursion overflows under AddressSanitizer - they ran"
	echo " for real in the section above)"
	make -C test clean >/dev/null
	if NANO_OS_TEST_SKIP_KERNEL=1 make -C test run ASAN=1; then
		echo "[asan] PASS"
	else
		echo "[asan] FAIL"
		rc=1
	fi
fi

if [ "${RUN_E2E}" = "1" ]; then
	section "Layer 3: end-to-end (real simulator)"
	if python3 -c "import pexpect" 2>/dev/null; then
		if python3 test/e2e/run.py; then
			echo "[e2e] PASS"
		else
			echo "[e2e] FAIL"
			rc=1
		fi
	else
		echo "[e2e] SKIP - python3 'pexpect' module not available"
	fi
fi

section "RESULT"
if [ "${rc}" = "0" ]; then
	echo "ALL LAYERS PASSED"
else
	echo "ONE OR MORE LAYERS FAILED (see above; known-bug tests stay red)"
fi
exit "${rc}"
