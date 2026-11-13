# Linux Programming Project 2

This project presents four Linux programming investigations covering process management and inter-process communication, comparative file-copying performance, multithreaded computation using POSIX threads, and concurrent log file processing. The implementations demonstrate practical application of C programming, Linux system calls, file I/O, process creation, thread synchronization, and performance benchmarking techniques.

---

## Compilation & Execution

### Question 1: Three-Stage Command Pipeline

```bash
cd question1
gcc -Wall -Wextra -Werror -pedantic -std=c11 pipeline.c -o pipeline
./pipeline
```

The program executes a three-stage command pipeline and writes the output to `result.txt`.

To inspect system calls:

```bash
strace -f -o trace.txt ./pipeline
```

### Question 2: Comparing File Copying Performance

```bash
cd question2
chmod +x run_tests.sh
./run_tests.sh
```

The script compiles both implementations, tests different buffer sizes, verifies copied files using `cmp`, and collects performance and system-call measurements.

### Question 3: Multithreaded Perfect Number Search

```bash
cd question3
gcc -Wall -Wextra -Wpedantic -O2 perfect_no.c -o perfect_no -pthread -lm
./perfect_no
```

To repeat the performance experiment five times:

```bash
for i in 1 2 3 4 5; do
    echo "Run $i"
    ./perfect_no
done
```

### Question 4: Multithreaded Log File Scanner

```bash
cd question4
chmod +x run_benchmark.sh
./run_benchmark.sh
```

The script compiles the program, generates test log files, and benchmarks execution using 1, 2, 4, and 8 threads across three runs per configuration.

Results are saved in `results.csv` and `summary.csv`.

The scanner can also be executed directly:

```bash
./logscan <pattern> <report.txt> <number_of_threads> <file1.log> [file2.log ...]
```

---

## Summary of Projects

### Question 1: Implementing a Three-Stage Command Pipeline

- Implements a three-stage command pipeline in C using Linux process management and inter-process communication.
- Uses `fork()`, `pipe()`, `dup2()`, and `exec()` to connect commands and manage process execution.
- Uses `strace` to investigate system calls involved in process creation, execution, and communication.

### Question 2: Comparing File Copying Performance

- Develops two file-copying implementations: `copy_sys`, using Linux system calls, and `copy_stdio`, using standard I/O functions.
- Evaluates performance across different buffer sizes and compares execution times.
- Uses `cmp` to verify file integrity and `strace -c` to examine system-call behaviour.

### Question 3: Multithreaded Perfect Number Search Using POSIX Threads

- Implements single-threaded and multithreaded searches for perfect numbers between 1 and 100,000.
- Uses eight POSIX threads to divide the search range into equal segments, with mutex synchronization for shared results.
- Compares execution times across repeated runs to evaluate the performance benefits of multithreading.

### Question 4: Multithreaded Log File Scanner Using POSIX Threads

- Develops a concurrent log file scanner that searches multiple files for a specified pattern and counts total lines and matching lines.
- Uses a shared work queue and mutex synchronization to coordinate worker threads.
- Benchmarks performance using different thread counts and evaluates execution time and speedup.

---

## AI Assistance Used

AI tools were used to support:

- Understanding Linux system programming concepts and programming interfaces.
- Debugging and troubleshooting implementation issues.
- Reviewing implementations against assignment requirements.
- Supporting design, execution, and interpretation of performance benchmarking experiments.
- Improving technical documentation and report clarity.

All implementations were compiled and tested in Linux environment, with experimental results obtained from program execution.

---

## Resources

The following resources were used for learning and reference during the development of these projects:

- [Linux man-pages](https://man7.org/linux/man-pages/)
- [GNU Compiler Collection (GCC) Documentation](https://gcc.gnu.org/onlinedocs/)
- [GNU Coreutils Documentation](https://www.gnu.org/software/coreutils/manual/)
- [strace Documentation](https://strace.io/)
- [POSIX Threads Documentation](https://man7.org/linux/man-pages/man7/pthreads.7.html)

---

## License

This project is under the MIT License.

---

## Author

Monica Dhieu

