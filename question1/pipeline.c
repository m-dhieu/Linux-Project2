#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#define BUFFER_SIZE 4096
#define OUTPUT_FILE "result.txt"

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void write_all(int fd, const char *buf, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, buf + written, size - written);
        if (n == -1) {
            if (errno == EINTR) continue;
            error("write");
        }
	if (n == 0) {
	   fprintf(stderr, "write returned zero bytes\n");
	   exit(EXIT_FAILURE);
	}
        written += (size_t)n;
    }
}

void close_pipes(int p1[], int p2[], int p3[]) {
    close(p1[0]); close(p1[1]);
    close(p2[0]); close(p2[1]);
    close(p3[0]); close(p3[1]);
}

void fork_failure(pid_t children[], int count,
                  int p1[], int p2[], int p3[]) {
    perror("fork");
    close_pipes(p1, p2, p3);
    for (int i = 0; i < count; i++) kill(children[i], SIGTERM);
    for (int i = 0; i < count; i++) {
        while (waitpid(children[i], NULL, 0) == -1) {
            if (errno == EINTR) continue;
            break;
        }
    }
    exit(EXIT_FAILURE);
}

int wait_child(pid_t pid, const char *name) {
    int status;
    pid_t result;

    do {result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("waitpid");
        return 0;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("%s exited with status %d\n", name, code);
        return code == 0;
    }
    if (WIFSIGNALED(status))
        printf("%s terminated by signal %d\n", name, WTERMSIG(status));

    return 0;
}

int main(void) {
    int p1[2], p2[2], p3[2];
    if (pipe(p1) == -1 || pipe(p2) == -1 || pipe(p3) == -1) error("pipe");
    
    pid_t children[3];
    /* child 1: cat /etc/passwd */
    children[0] = fork();
    if (children[0] == -1) fork_failure(children, 0, p1, p2, p3);
    if (children[0] == 0) {
        if (dup2(p1[1], STDOUT_FILENO) == -1) {
            perror("dup2 child 1");
            _exit(127);
        }
        close_pipes(p1, p2, p3);
        char *args[] = {"cat", "/etc/passwd", NULL};
        execvp(args[0], args);
        perror("execvp cat");
        _exit(127);
    }

    /* child 2: cut -d: -f7 */
    children[1] = fork();
    if (children[1] == -1) fork_failure(children, 1, p1, p2, p3);
    if (children[1] == 0) {
        if (dup2(p1[0], STDIN_FILENO) == -1 ||
            dup2(p2[1], STDOUT_FILENO) == -1) {
            perror("dup2 child 2");
            _exit(127);
        }
        close_pipes(p1, p2, p3);
        char *args[] = {"cut", "-d:", "-f7", NULL};
        execvp(args[0], args);
        perror("execvp cut");
        _exit(127);
    }

    /* child 3: sort -u */
    children[2] = fork();
    if (children[2] == -1) fork_failure(children, 2, p1, p2, p3);
    if (children[2] == 0) {
        if (dup2(p2[0], STDIN_FILENO) == -1 ||
            dup2(p3[1], STDOUT_FILENO) == -1) {
            perror("dup2 child 3");
            _exit(127);
        }
        close_pipes(p1, p2, p3);
        char *args[] = {"sort", "-u", NULL};
        execvp(args[0], args);
        perror("execvp sort");
        _exit(127);
    }

    /* parent: close unused || ends */
    close(p1[0]); close(p1[1]);
    close(p2[0]); close(p2[1]);
    close(p3[1]);

    /* read || output & save */
    int fd = open(OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) error("open output file");
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while (1) {
        n = read(p3[0], buffer, sizeof(buffer));
        if (n == -1 && errno == EINTR) continue;
        if (n == -1) error("read pipe");
        if (n == 0) break;
        write_all(fd, buffer, (size_t)n);
    }
    close(p3[0]);
    close(fd);

    /* reopen & display 1st 5 lines */
    fd = open(OUTPUT_FILE, O_RDONLY);
    if (fd == -1) error("open result file");
    size_t line_count = 0;
    int has_data = 0;
    char last_char = '\0';
    printf("\n First 5 lines of %s \n", OUTPUT_FILE);
    fflush(stdout);

    while (1) {
        n = read(fd, buffer, sizeof(buffer));
        if (n == -1 && errno == EINTR) continue;
        if (n == -1) error("read file");
        if (n == 0) break;
        size_t print_len = 0;
        for (ssize_t i = 0; i < n; i++) {
            has_data = 1;
            last_char = buffer[i];
            if (line_count < 5) print_len = (size_t)i + 1;
            if (buffer[i] == '\n') line_count++;
        }
        if (print_len > 0)
            write_all(STDOUT_FILENO, buffer, print_len);
    }

    if (has_data && last_char != '\n') line_count++;
    close(fd);

    printf("\n\n");
    printf("Total line count: %zu\n", line_count);
    printf("\n Child process statuses \n");

    int success = 1;
    if (!wait_child(children[0], "Child 1 (cat)")) success = 0;
    if (!wait_child(children[1], "Child 2 (cut)")) success = 0;
    if (!wait_child(children[2], "Child 3 (sort)")) success = 0;
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

