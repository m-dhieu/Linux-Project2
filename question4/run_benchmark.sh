#!/bin/bash

set -euo pipefail

echo ""
echo "LOGSCAN PERFORMANCE EXPERIMENT"

echo
echo "Compiling logscan.c..."
gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread logscan.c -o logscan

echo
echo "Generating log files..."
mkdir -p test_logs
SIZES=(1 2 3 5 8 10 12 15 20 25 30 40 45 50)
for i in "${!SIZES[@]}"; do
    SIZE_MB=${SIZES[$i]}
    TARGET_BYTES=$((SIZE_MB * 1024 * 1024))
    FILE="test_logs/server_$((i+1))_${SIZE_MB}MB.log"
    awk -v target="$TARGET_BYTES" 'BEGIN {
        i = 0
        bytes = 0
        while (bytes < target) {
            i++
            if (i % 17 == 0)
                line = sprintf("ERROR: Connection timeout on worker node %d", i % 64)
            else
                line = sprintf("INFO: Request processed successfully id=%d", i)
            print line
            bytes += length(line) + 1
        }
    }' > "$FILE"
done

echo
echo "Generated file sizes:"
for FILE in test_logs/*.log; do
    SIZE=$(stat -c%s "$FILE")
    echo "$FILE: $SIZE bytes"
done

echo
echo "Ground verification:"
GREP_TOTAL=0
for FILE in test_logs/*.log; do
    COUNT=$(grep -c 'ERROR' "$FILE" || true)
    GREP_TOTAL=$((GREP_TOTAL + COUNT))
done
echo "Expected total matching lines: $GREP_TOTAL"

echo
echo "Missing-file test..."
./logscan "ERROR" report_err_test.txt 4 \
    test_logs/server_1_1MB.log nonexistent_file.log \
    > error_test_output.txt 2> error_test_errors.txt

grep -q "Total matching lines across 1 files:" error_test_output.txt
grep -q "nonexistent_file.log" error_test_errors.txt
echo "Missing-file test passed."

echo
echo "Running performance experiments..."
echo "Threads,Run,ElapsedSeconds" > results.csv
echo "Threads,AverageSeconds,Speedup" > summary.csv

declare -A AVERAGES

# run each config 3 times
for T in 1 2 4 8; do
    SUM=0
    echo
    echo "Testing $T thread(s)..."

    for RUN in 1 2 3; do
        OUT=$(./logscan "ERROR" "report_t${T}.txt" \
              "$T" test_logs/*.log)
        MATCHES=$(echo "$OUT" |
                  awk '/^Total matching lines/ {print $NF}')
        ELAPSED=$(echo "$OUT" |
                  awk '/^Elapsed time/ {print $5}')
        if [ "$MATCHES" != "$GREP_TOTAL" ]; then
            echo "ERROR: Grand total mismatch."
            echo "Expected: $GREP_TOTAL"
            echo "Actual: $MATCHES"
            exit 1
        fi

        for FILE in test_logs/*.log; do
            EXPECTED=$(grep -c 'ERROR' "$FILE" || true)
            ACTUAL=$(awk -F': ' -v file="$FILE" \
                '$1 == file {
                    split($2, a, " ")
                    print a[1]
                }' "report_t${T}.txt")
            if [ "$EXPECTED" != "$ACTUAL" ]; then
                echo "ERROR: Verification failed for $FILE"
                echo "Expected: $EXPECTED"
                echo "Actual: $ACTUAL"
                exit 1
            fi
        done

        SUM=$(awk -v s="$SUM" -v t="$ELAPSED" \
              'BEGIN {printf "%.9f", s+t}')
        echo "$T,$RUN,$ELAPSED" >> results.csv
        echo "Run $RUN: ${ELAPSED}s - Verified"

    done

    AVG=$(awk -v s="$SUM" \
          'BEGIN {printf "%.6f", s/3}')
    AVERAGES[$T]=$AVG
    echo "Average ($T threads): ${AVG}s"

done

# speedup calc
echo
echo "Performance summary..."

BASELINE=${AVERAGES[1]}
printf "\n%-12s %-20s %-15s\n" \
       "Threads" "Average Time (s)" "Speedup"

for T in 1 2 4 8; do
    AVG=${AVERAGES[$T]}
    SPEEDUP=$(awk -v base="$BASELINE" -v avg="$AVG" \
        'BEGIN {
            if (avg > 0)
                printf "%.2f", base/avg
            else
                print "N/A"
        }')
    printf "%-12s %-20s %-15s\n" \
           "$T" "$AVG" "${SPEEDUP}x"
    echo "$T,$AVG,$SPEEDUP" >> summary.csv
done

echo
echo "EXPERIMENT COMPLETED"
echo ""
echo "Individual timings: results.csv"
echo "Average timings: summary.csv"
echo "Generated logs: test_logs/"
echo ""

