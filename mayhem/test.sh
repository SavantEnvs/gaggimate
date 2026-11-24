#!/usr/bin/env bash
# Run upstream's native Unity unit suites (test/test_*, the `pio test -e native` set), built by
# mayhem/build.sh with normal flags. They assert real values: OTA resume/retry against a scripted
# server (the same ResumableDownloader the fuzz_ota_download target fuzzes), PID/autotune maths,
# button handling, heater coordination and the shot-log layout.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Expected number of Unity test cases per suite: a suite that runs fewer (or prints no summary)
# counts its missing cases as failed.
declare -A EXPECT=([autotune_simc]=5 [button_handler]=13 [heater_coordinator]=29 [ota_download]=23
                   [puckflow_latch]=2 [shot_log_layout]=1 [simple_pid_rate]=3)
passed=0 failed=0 skipped=0
for t in autotune_simc button_handler heater_coordinator ota_download puckflow_latch shot_log_layout simple_pid_rate; do
  bin="out/tests/test_$t"
  if [ ! -x "$bin" ]; then
    echo "test.sh: $bin missing (build.sh should have built it)" >&2
    failed=$(( failed + EXPECT[$t] )); continue
  fi
  log=$("$bin" 2>&1); rc=$?
  printf '%s\n' "$log" | grep -E ':(PASS|FAIL|IGNORE)' | sed "s/^/[$t] /" | head -200
  sum=$(printf '%s\n' "$log" | grep -E '^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' | tail -1)
  if [ -z "$sum" ]; then
    echo "test.sh: $t printed no Unity summary (rc=$rc)" >&2
    failed=$(( failed + EXPECT[$t] )); continue
  fi
  read -r n _ f _ i _ <<<"$sum"
  p=$(( n - f - i ))
  # Unity prints ":PASS" for every passing case; trust only the cases it reported passing.
  seen=$(printf '%s\n' "$log" | grep -c ':PASS')
  [ "$seen" -lt "$p" ] && p=$seen
  missing=$(( EXPECT[$t] - p - f - i )); [ "$missing" -lt 0 ] && missing=0
  [ "$rc" -ne 0 ] && [ "$f" -eq 0 ] && f=1
  passed=$(( passed + p )); failed=$(( failed + f + missing )); skipped=$(( skipped + i ))
done
emit_ctrf unity "$passed" "$failed" "$skipped"
