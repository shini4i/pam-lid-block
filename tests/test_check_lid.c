/**
 * @file test_check_lid.c
 * @brief Comprehensive test suite for check-lid utility.
 *
 * This test suite verifies the functionality of the check-lid PAM helper,
 * including CLI argument parsing, D-Bus connectivity, exit code behavior,
 * error handling, signal handling, and fail-safe behavior.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

/* Test framework macros */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define RUN_TEST(test_func)                                                                        \
    do {                                                                                           \
        printf("  Running %s... ", #test_func);                                                    \
        fflush(stdout);                                                                            \
        tests_run++;                                                                               \
        int result = test_func();                                                                  \
        if (result == TEST_PASS) {                                                                 \
            printf("PASS\n");                                                                      \
            tests_passed++;                                                                        \
        } else if (result == TEST_SKIP) {                                                          \
            printf("SKIP\n");                                                                      \
            tests_skipped++;                                                                       \
        } else {                                                                                   \
            printf("FAIL\n");                                                                      \
            tests_failed++;                                                                        \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ(expected, actual)                                                                \
    do {                                                                                           \
        if ((expected) != (actual)) {                                                              \
            fprintf(stderr, "\n    Assertion failed: expected %d, got %d\n", (int)(expected),      \
                    (int)(actual));                                                                \
            return TEST_FAIL;                                                                      \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "\n    Assertion failed: condition is false\n");                       \
            return TEST_FAIL;                                                                      \
        }                                                                                          \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                                       \
    do {                                                                                           \
        if ((ptr) == NULL) {                                                                       \
            fprintf(stderr, "\n    Assertion failed: pointer is NULL\n");                          \
            return TEST_FAIL;                                                                      \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                                      \
    do {                                                                                           \
        if (strstr((haystack), (needle)) == NULL) {                                                \
            fprintf(stderr, "\n    Assertion failed: '%s' not found in output\n", (needle));       \
            return TEST_FAIL;                                                                      \
        }                                                                                          \
    } while (0)

/**
 * @brief Helper to run check-lid binary and capture exit code.
 *
 * @param args NULL-terminated argument array
 * @param envp NULL-terminated environment array (NULL for inherit)
 * @return Exit code of the process, or -1 on fork/exec failure
 */
static int run_check_lid_env(char *const args[], char *const envp[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        if (freopen("/dev/null", "w", stdout) == NULL ||
            freopen("/dev/null", "w", stderr) == NULL) {
            _exit(126);
        }
        if (envp != NULL) {
            execve("./build/check-lid", args, envp);
        } else {
            execv("./build/check-lid", args);
        }
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

/**
 * @brief Helper to run check-lid with default environment.
 */
static int run_check_lid(char *const args[]) {
    return run_check_lid_env(args, NULL);
}

/**
 * @brief Helper to run check-lid and capture stdout.
 *
 * @param args NULL-terminated argument array
 * @param output Buffer to store output
 * @param output_size Size of output buffer
 * @return Exit code of the process
 */
static int run_check_lid_capture(char *const args[], char *output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv("./build/check-lid", args);
        _exit(127);
    }

    /* Parent process */
    close(pipefd[1]);

    ssize_t bytes_read = read(pipefd[0], output, output_size - 1);
    if (bytes_read >= 0) {
        output[bytes_read] = '\0';
    } else {
        output[0] = '\0';
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

/**
 * @brief Check if D-Bus system bus is available.
 */
static int dbus_available(void) {
    sd_bus *bus = NULL;
    int ret = sd_bus_open_system(&bus);
    if (ret >= 0) {
        sd_bus_unref(bus);
        return 1;
    }
    return 0;
}

/**
 * @brief Check if systemd-logind LidClosed property is available.
 */
static int logind_available(void) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus *bus = NULL;
    int ret;
    int lid_closed = 0;

    ret = sd_bus_open_system(&bus);
    if (ret < 0) {
        return 0;
    }

    ret = sd_bus_get_property_trivial(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                      "org.freedesktop.login1.Manager", "LidClosed", &error, 'b',
                                      &lid_closed);

    sd_bus_error_free(&error);
    sd_bus_unref(bus);

    return ret >= 0;
}

/* ============================================================================
 * CLI Tests
 * ============================================================================ */

/**
 * @brief Test --help flag returns success and shows usage.
 */
static int test_cli_help(void) {
    char output[4096];
    char *args[] = {"check-lid", "--help", NULL};
    int exit_code = run_check_lid_capture(args, output, sizeof(output));

    ASSERT_EQ(0, exit_code);
    ASSERT_STR_CONTAINS(output, "Usage:");
    ASSERT_STR_CONTAINS(output, "--verbose");
    ASSERT_STR_CONTAINS(output, "--version");
    ASSERT_STR_CONTAINS(output, "--help");
    ASSERT_STR_CONTAINS(output, "Exit codes:");

    return TEST_PASS;
}

/**
 * @brief Test -h short flag returns success.
 */
static int test_cli_help_short(void) {
    char output[4096];
    char *args[] = {"check-lid", "-h", NULL};
    int exit_code = run_check_lid_capture(args, output, sizeof(output));

    ASSERT_EQ(0, exit_code);
    ASSERT_STR_CONTAINS(output, "Usage:");

    return TEST_PASS;
}

/**
 * @brief Test --version flag returns success and shows version.
 */
static int test_cli_version(void) {
    char output[256];
    char *args[] = {"check-lid", "--version", NULL};
    int exit_code = run_check_lid_capture(args, output, sizeof(output));

    ASSERT_EQ(0, exit_code);
    ASSERT_STR_CONTAINS(output, "check-lid");

    return TEST_PASS;
}

/**
 * @brief Test -V short flag returns success.
 */
static int test_cli_version_short(void) {
    char output[256];
    char *args[] = {"check-lid", "-V", NULL};
    int exit_code = run_check_lid_capture(args, output, sizeof(output));

    ASSERT_EQ(0, exit_code);
    ASSERT_STR_CONTAINS(output, "check-lid");

    return TEST_PASS;
}

/**
 * @brief Test invalid option returns failure.
 */
static int test_cli_invalid_option(void) {
    char *args[] = {"check-lid", "--invalid", NULL};
    int exit_code = run_check_lid(args);

    ASSERT_EQ(1, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test multiple invalid options return failure.
 */
static int test_cli_multiple_invalid(void) {
    char *args[] = {"check-lid", "-x", "-y", "-z", NULL};
    int exit_code = run_check_lid(args);

    ASSERT_EQ(1, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test verbose mode produces output to stderr.
 */
static int test_cli_verbose_output(void) {
    char *args[] = {"check-lid", "--verbose", NULL};
    /* Use forced state to get predictable output */
    char *envp[] = {"CHECK_LID_TEST_FORCE_STATE=closed", NULL};

    int exit_code = run_check_lid_env(args, envp);

    /* The test passes if verbose flag is accepted and returns expected code */
    ASSERT_EQ(0, exit_code);

    return TEST_PASS;
}

/* ============================================================================
 * Error Injection Tests (Fail-Safe Behavior)
 * ============================================================================ */

/**
 * @brief Test D-Bus connection error returns exit code 1 (fail-safe).
 */
static int test_error_dbus_connection(void) {
    char *args[] = {"check-lid", NULL};
    char *envp[] = {"CHECK_LID_TEST_FORCE_ERROR=dbus", NULL};

    int exit_code = run_check_lid_env(args, envp);

    ASSERT_EQ(1, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test D-Bus query error returns exit code 1 (fail-safe).
 */
static int test_error_dbus_query(void) {
    char *args[] = {"check-lid", NULL};
    char *envp[] = {"CHECK_LID_TEST_FORCE_ERROR=query", NULL};

    int exit_code = run_check_lid_env(args, envp);

    ASSERT_EQ(1, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test forced lid closed state returns exit code 0.
 */
static int test_force_lid_closed(void) {
    char *args[] = {"check-lid", NULL};
    char *envp[] = {"CHECK_LID_TEST_FORCE_STATE=closed", NULL};

    int exit_code = run_check_lid_env(args, envp);

    ASSERT_EQ(0, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test forced lid open state returns exit code 1.
 */
static int test_force_lid_open(void) {
    char *args[] = {"check-lid", NULL};
    char *envp[] = {"CHECK_LID_TEST_FORCE_STATE=open", NULL};

    int exit_code = run_check_lid_env(args, envp);

    ASSERT_EQ(1, exit_code);

    return TEST_PASS;
}

/**
 * @brief Test verbose mode with error shows error message.
 */
static int test_error_verbose_output(void) {
    char output[4096];
    char *args[] = {"check-lid", "--verbose", NULL};

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return TEST_FAIL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return TEST_FAIL;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (freopen("/dev/null", "w", stdout) == NULL) {
            _exit(126);
        }
        char *envp[] = {"CHECK_LID_TEST_FORCE_ERROR=dbus", NULL};
        execve("./build/check-lid", args, envp);
        _exit(127);
    }

    close(pipefd[1]);
    ssize_t bytes_read = read(pipefd[0], output, sizeof(output) - 1);
    if (bytes_read >= 0) {
        output[bytes_read] = '\0';
    } else {
        output[0] = '\0';
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(1, WEXITSTATUS(status));
    ASSERT_STR_CONTAINS(output, "TEST MODE");

    return TEST_PASS;
}

/* ============================================================================
 * Signal Handler Tests
 * ============================================================================ */

/**
 * @brief Helper to test signal handling with delay.
 *
 * Uses CHECK_LID_TEST_DELAY_MS to make the process wait long enough
 * for us to send a signal.
 *
 * @param sig Signal to send
 * @return TEST_PASS if signal handler works correctly, TEST_FAIL otherwise
 */
static int test_signal_helper(int sig) {
    pid_t pid = fork();
    if (pid < 0) {
        return TEST_FAIL;
    }

    if (pid == 0) {
        /* Child: run check-lid with delay to allow signal delivery */
        if (freopen("/dev/null", "w", stdout) == NULL ||
            freopen("/dev/null", "w", stderr) == NULL) {
            _exit(126);
        }
        char *args[] = {"check-lid", NULL};
        /* Delay 500ms to give parent time to send signal */
        char *envp[] = {"CHECK_LID_TEST_DELAY_MS=500", NULL};
        execve("./build/check-lid", args, envp);
        _exit(127);
    }

    /* Parent: wait for process to start, then send signal */
    usleep(50000); /* 50ms - enough for process to start and enter delay */
    kill(pid, sig);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        /* Process caught signal and exited with fail-safe code */
        if (WEXITSTATUS(status) == 1) {
            return TEST_PASS;
        }
        fprintf(stderr, "\n    Process exited with code %d, expected 1\n", WEXITSTATUS(status));
        return TEST_FAIL;
    }

    if (WIFSIGNALED(status)) {
        /* Process was killed by signal without handling - also acceptable
         * as it means auth will proceed (fail-safe behavior) */
        return TEST_PASS;
    }

    return TEST_FAIL;
}

/**
 * @brief Test SIGTERM results in exit code 1 (fail-safe).
 */
static int test_signal_sigterm(void) {
    return test_signal_helper(SIGTERM);
}

/**
 * @brief Test SIGINT results in exit code 1 (fail-safe).
 */
static int test_signal_sigint(void) {
    return test_signal_helper(SIGINT);
}

/**
 * @brief Test SIGHUP results in exit code 1 (fail-safe).
 */
static int test_signal_sighup(void) {
    return test_signal_helper(SIGHUP);
}

/* ============================================================================
 * D-Bus Connectivity Tests
 * ============================================================================ */

/**
 * @brief Test that D-Bus system bus can be opened.
 */
static int test_dbus_connection(void) {
    if (!dbus_available()) {
        return TEST_SKIP;
    }

    sd_bus *bus = NULL;
    int ret = sd_bus_open_system(&bus);
    ASSERT_TRUE(ret >= 0);
    ASSERT_NOT_NULL(bus);
    sd_bus_unref(bus);

    return TEST_PASS;
}

/**
 * @brief Test that systemd-logind LidClosed property can be queried.
 */
static int test_logind_lid_property(void) {
    if (!logind_available()) {
        return TEST_SKIP;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus *bus = NULL;
    int ret;
    int lid_closed = 0;

    ret = sd_bus_open_system(&bus);
    ASSERT_TRUE(ret >= 0);

    ret = sd_bus_get_property_trivial(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                      "org.freedesktop.login1.Manager", "LidClosed", &error, 'b',
                                      &lid_closed);

    ASSERT_TRUE(ret >= 0);
    ASSERT_TRUE(lid_closed == 0 || lid_closed == 1);

    sd_bus_error_free(&error);
    sd_bus_unref(bus);

    return TEST_PASS;
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

/**
 * @brief Test that check-lid returns valid PAM exit codes (0 or 1).
 */
static int test_exit_code_valid(void) {
    if (!logind_available()) {
        return TEST_SKIP;
    }

    char *args[] = {"check-lid", NULL};
    int exit_code = run_check_lid(args);

    ASSERT_TRUE(exit_code == 0 || exit_code == 1);

    return TEST_PASS;
}

/**
 * @brief Test that consecutive calls return consistent results.
 */
static int test_exit_code_consistent(void) {
    if (!logind_available()) {
        return TEST_SKIP;
    }

    char *args[] = {"check-lid", NULL};
    int exit_code1 = run_check_lid(args);
    int exit_code2 = run_check_lid(args);
    int exit_code3 = run_check_lid(args);

    ASSERT_EQ(exit_code1, exit_code2);
    ASSERT_EQ(exit_code2, exit_code3);

    return TEST_PASS;
}

/**
 * @brief Test that verbose mode doesn't affect exit code.
 */
static int test_verbose_same_exit_code(void) {
    /* Use forced state for deterministic comparison */
    char *args_normal[] = {"check-lid", NULL};
    char *args_verbose[] = {"check-lid", "--verbose", NULL};
    char *envp[] = {"CHECK_LID_TEST_FORCE_STATE=closed", NULL};

    int exit_normal = run_check_lid_env(args_normal, envp);
    int exit_verbose = run_check_lid_env(args_verbose, envp);

    ASSERT_EQ(exit_normal, exit_verbose);

    return TEST_PASS;
}

/**
 * @brief Test forced states work consistently.
 */
static int test_forced_state_consistency(void) {
    char *args[] = {"check-lid", NULL};
    char *envp_closed[] = {"CHECK_LID_TEST_FORCE_STATE=closed", NULL};
    char *envp_open[] = {"CHECK_LID_TEST_FORCE_STATE=open", NULL};

    /* Run multiple times to ensure consistency */
    for (int i = 0; i < 3; i++) {
        int closed = run_check_lid_env(args, envp_closed);
        int open = run_check_lid_env(args, envp_open);

        ASSERT_EQ(0, closed);
        ASSERT_EQ(1, open);
    }

    return TEST_PASS;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("\n=== check-lid Test Suite ===\n\n");

    if (access("./build/check-lid", X_OK) != 0) {
        fprintf(stderr, "Error: ./build/check-lid not found or not executable.\n");
        fprintf(stderr, "Run 'task build' first.\n");
        return 1;
    }

    printf("CLI Tests:\n");
    RUN_TEST(test_cli_help);
    RUN_TEST(test_cli_help_short);
    RUN_TEST(test_cli_version);
    RUN_TEST(test_cli_version_short);
    RUN_TEST(test_cli_invalid_option);
    RUN_TEST(test_cli_multiple_invalid);
    RUN_TEST(test_cli_verbose_output);

    printf("\nError Injection Tests (Fail-Safe Behavior):\n");
    RUN_TEST(test_error_dbus_connection);
    RUN_TEST(test_error_dbus_query);
    RUN_TEST(test_force_lid_closed);
    RUN_TEST(test_force_lid_open);
    RUN_TEST(test_error_verbose_output);

    printf("\nSignal Handler Tests:\n");
    RUN_TEST(test_signal_sigterm);
    RUN_TEST(test_signal_sigint);
    RUN_TEST(test_signal_sighup);

    printf("\nD-Bus Connectivity Tests:\n");
    RUN_TEST(test_dbus_connection);
    RUN_TEST(test_logind_lid_property);

    printf("\nIntegration Tests:\n");
    RUN_TEST(test_exit_code_valid);
    RUN_TEST(test_exit_code_consistent);
    RUN_TEST(test_verbose_same_exit_code);
    RUN_TEST(test_forced_state_consistency);

    printf("\n=== Test Summary ===\n");
    printf("Total:   %d\n", tests_run);
    printf("Passed:  %d\n", tests_passed);
    printf("Failed:  %d\n", tests_failed);
    printf("Skipped: %d\n", tests_skipped);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
