#!/usr/bin/env bash
#
# Checks that the C++ build agrees with reference/aswap.py, which is the
# original Python implementation and the definition of aswap's semantics.
#
# Both modes are compared as sorted unique sets: the reference emits duplicates
# (it never dedups), the default mode dedups per input word and --unique dedups
# across the whole run, so only the set of distinct candidates has to match.

set -u

cd "$(dirname "$0")/.."

BIN=./aswap
REF=reference/aswap.py
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
pass=0

check() {
  local name="$1"; shift
  "$REF" "$@" < "$TMP/in.txt" 2>/dev/null | sort -u > "$TMP/ref.txt"
  "$BIN" "$@" < "$TMP/in.txt" 2>/dev/null | sort -u > "$TMP/default.txt"
  "$BIN" --unique "$@" < "$TMP/in.txt" 2>/dev/null | sort -u > "$TMP/unique.txt"

  local n
  n=$(wc -l < "$TMP/ref.txt" | tr -d ' ')

  for mode in default unique; do
    if diff -q "$TMP/ref.txt" "$TMP/$mode.txt" >/dev/null; then
      printf '  ok   %-22s %-7s (%s candidates)\n' "$name" "$mode" "$n"
      pass=$((pass + 1))
    else
      printf '  FAIL %-22s %-7s\n' "$name" "$mode"
      diff "$TMP/ref.txt" "$TMP/$mode.txt" | head -10
      fail=$((fail + 1))
    fi
  done
}

if [ ! -x "$BIN" ]; then
  echo "error: $BIN not built - run 'make' first" >&2
  exit 1
fi

echo "== rule equivalence vs $REF =="

printf 'kitties\nlove\npassword\nbooboo\nOOOOOO\naaaaaaaa\nhunter2\n' > "$TMP/in.txt"
check "single rule"        1e3
check "deeper single rule" 2e3
check "two rules"          2i! 2e3
check "three rules"        3o0 2a@ 2e3
check "max levels"         9a@
check "same char twice"    1a@ 1a4

printf 'love\nlove\nl\xc3\xb6ve\nlove\n' > "$TMP/in.txt"
check "duplicates + utf8"  1o0

echo "== rule validation (C++ and reference agree) =="
for bad in 0e3 09e zzz e3 2e33 ""; do
  label=${bad:-"(empty)"}
  cout=$(echo love | $BIN "$bad" 2>"$TMP/cerr"); crc=$?
  pout=$(echo love | "$REF" "$bad" 2>"$TMP/perr"); prc=$?
  if [ "$crc" -eq 1 ] && [ "$prc" -eq 1 ] && [ -z "$cout" ] && [ -z "$pout" ] \
     && diff -q "$TMP/cerr" "$TMP/perr" >/dev/null; then
    printf '  ok   rejected %-8s both exit 1, same message\n' "$label"
    pass=$((pass + 1))
  else
    printf '  FAIL rejected %-8s cpp(rc=%s out=%s) ref(rc=%s out=%s)\n' \
      "$label" "$crc" "${cout:-<none>}" "$prc" "${pout:-<none>}"
    diff "$TMP/cerr" "$TMP/perr" | head -4
    fail=$((fail + 1))
  fi
done

echo "== blank input lines are dropped (differs from reference by design) =="
printf '\n   \n\t\nlove\n' > "$TMP/in.txt"
for mode in "" "--unique"; do
  got=$($BIN $mode 1o0 < "$TMP/in.txt" | sort -u | tr '\n' ' ')
  if [ "$got" = "l0ve love " ]; then
    printf '  ok   blank lines dropped    %-8s\n' "${mode:-default}"
    pass=$((pass + 1))
  else
    printf '  FAIL blank lines dropped    %-8s (got: %s)\n' "${mode:-default}" "$got"
    fail=$((fail + 1))
  fi
done

echo "== long lines (no 4 KiB limit) =="
python3 -c "print('a' * 5000)" > "$TMP/in.txt"
for mode in "" "--unique"; do
  got=$($BIN $mode 1a@ < "$TMP/in.txt" | wc -l | tr -d ' ')
  if [ "$got" = "2" ]; then
    printf '  ok   5000-char line        %-8s\n' "${mode:-default}"
    pass=$((pass + 1))
  else
    printf '  FAIL 5000-char line        %-8s (got %s lines, want 2)\n' "${mode:-default}" "$got"
    fail=$((fail + 1))
  fi
done

echo "== default mode emits before stdin closes =="
# Feed 3 words with a pause between them; the default must produce output
# before the last word is written, --unique must not.
for mode in "" "--unique"; do
  start=$(date +%s)
  first=$( { for i in 1 2 3; do echo "password$i"; sleep 1; done; } \
           | $BIN $mode 1o0 2>/dev/null \
           | { read -r _line; date +%s; } )
  elapsed=$((first - start))
  if [ -z "$mode" ]; then
    if [ "$elapsed" -lt 2 ]; then
      printf '  ok   default first line after %ss (input runs 3s)\n' "$elapsed"
      pass=$((pass + 1))
    else
      printf '  FAIL default first line after %ss, expected < 2s\n' "$elapsed"
      fail=$((fail + 1))
    fi
  else
    if [ "$elapsed" -ge 2 ]; then
      printf '  ok   --unique first line after %ss (waits for EOF, as documented)\n' "$elapsed"
      pass=$((pass + 1))
    else
      printf '  FAIL --unique first line after %ss, expected >= 2s\n' "$elapsed"
      fail=$((fail + 1))
    fi
  fi
done

echo "== --unique removes cross-word duplicates =="
printf 'love\nl0ve\n' > "$TMP/in.txt"
d=$($BIN 1o0 < "$TMP/in.txt" | wc -l | tr -d ' ')
u=$($BIN --unique 1o0 < "$TMP/in.txt" | wc -l | tr -d ' ')
if [ "$d" = "3" ] && [ "$u" = "2" ]; then
  printf '  ok   default=%s lines, --unique=%s lines\n' "$d" "$u"
  pass=$((pass + 1))
else
  printf '  FAIL default=%s (want 3), --unique=%s (want 2)\n' "$d" "$u"
  fail=$((fail + 1))
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "all $pass checks passed"
else
  echo "$fail of $((pass + fail)) checks FAILED"
fi
exit $((fail > 0))
