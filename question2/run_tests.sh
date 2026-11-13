#!/bin/bash

set -euo pipefail

SOURCE="big.bin"
DESTINATION="out.bin"
BUFFERS=(512 4096 65536)
BENCHMARK_FILE="benchmark_results.txt"
STRACE_FILE="strace_results.txt"

echo "Checking required tools..."
for tool in gcc dd cmp awk strace sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool is not installed."
        exit 1
    fi
done
echo "All required tools are available."
echo ""

echo "Compiling programs..."
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic copy_sys.c -o copy_sys
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic copy_stdio.c -o copy_stdio
echo "Both programs compiled successfully :)"
echo ""

echo "Testing missing-file error handling..."
if ./copy_sys missing_file.bin out.bin 4096; then
    echo "ERROR: copy_sys should have failed."
    exit 1
else
    echo "copy_sys correctly reported missing file."
fi
if ./copy_stdio missing_file.bin out.bin 4096; then
    echo "ERROR: copy_stdio should have failed."
    exit 1
else
    echo "copy_stdio correctly reported missing file."
fi
echo ""

echo "Creating 100 MB test file..."
dd if=/dev/urandom of="$SOURCE" bs=1M count=100 status=none
echo "Test file created:"
ls -lh "$SOURCE"
echo ""
echo "Source SHA256 checksum:"
sha256sum "$SOURCE"
echo ""

echo "Warming up both programs..."
./copy_sys "$SOURCE" "$DESTINATION" 4096 > /dev/null
./copy_stdio "$SOURCE" "$DESTINATION" 4096 > /dev/null
rm -f "$DESTINATION"
echo ""

echo "Normal benchmark results..."
printf "%-12s | %-10s | %-12s | %-12s | %-12s | %-12s\n" \
    "Program" "Buffer" "Run 1" "Run 2" "Run 3" "Average" \
    | tee "$BENCHMARK_FILE"
echo "--------------------------------------------------------------------------------" \
    | tee -a "$BENCHMARK_FILE"

for buf in "${BUFFERS[@]}"; do
    sys_runs=()
    stdio_runs=()
    sys_sum=0
    stdio_sum=0

    for i in 1 2 3; do
        # alternate execution order
        if (( i % 2 == 1 )); then ORDER=(copy_sys copy_stdio)
        else ORDER=(copy_stdio copy_sys)
        fi
        for prog in "${ORDER[@]}"; do
            rm -f "$DESTINATION"
            output=$(./"$prog" "$SOURCE" "$DESTINATION" "$buf")
            # verify copied file
            if ! cmp -s "$SOURCE" "$DESTINATION"; then
                echo "ERROR: Copy verification failed for $prog."
                exit 1
            fi
            elapsed=$(echo "$output" | awk '{print $3}')

            if [[ "$prog" == "copy_sys" ]]; then
                sys_runs+=("$elapsed")
                sys_sum=$(awk "BEGIN {print $sys_sum + $elapsed}")
            else
                stdio_runs+=("$elapsed")
                stdio_sum=$(awk "BEGIN {print $stdio_sum + $elapsed}")
            fi
        done
    done

    # averages
    sys_avg=$(awk "BEGIN {printf \"%.6f\", $sys_sum / 3}")
    stdio_avg=$(awk "BEGIN {printf \"%.6f\", $stdio_sum / 3}")
    # print & save results
    printf "%-12s | %-10s | %-12s | %-12s | %-12s | %-12s\n" \
        "copy_sys" "$buf" \
        "${sys_runs[0]}" "${sys_runs[1]}" "${sys_runs[2]}" \
        "$sys_avg" | tee -a "$BENCHMARK_FILE"
    printf "%-12s | %-10s | %-12s | %-12s | %-12s | %-12s\n" \
        "copy_stdio" "$buf" \
        "${stdio_runs[0]}" "${stdio_runs[1]}" "${stdio_runs[2]}" \
        "$stdio_avg" | tee -a "$BENCHMARK_FILE"

done
echo ""
echo "Benchmark results saved to $BENCHMARK_FILE"
echo ""

echo "Strace system call analysis... "
: > "$STRACE_FILE"
for buf in "${BUFFERS[@]}"; do
    for prog in copy_sys copy_stdio; do
        rm -f "$DESTINATION"
        # echo ""
        echo "" | tee -a "$STRACE_FILE"
        echo "Program: $prog | Buffer: $buf bytes" | tee -a "$STRACE_FILE"
        echo "" | tee -a "$STRACE_FILE"
        strace -c -o strace_temp.txt \
            ./"$prog" "$SOURCE" "$DESTINATION" "$buf"
        cat strace_temp.txt | tee -a "$STRACE_FILE"
        if ! cmp -s "$SOURCE" "$DESTINATION"; then
            echo "ERROR: Copy verification failed under strace."
            exit 1
        fi
    done
done

# clean temp files
rm -f "$DESTINATION" strace_temp.txt

echo ""
echo "ALL TESTS COMPLETED SUCCESSFULLY"
echo ""
echo "Benchmark results: $BENCHMARK_FILE"
echo "strace results:    $STRACE_FILE"
echo "Source file:       $SOURCE"
echo ""
echo "Source checksum:"
sha256sum "$SOURCE"

