/**
 * @file check-lid.c
 * @brief PAM helper to check laptop lid state via systemd-logind D-Bus API.
 *
 * Queries the LidClosed property from org.freedesktop.login1.Manager to
 * determine if the laptop lid is closed. Used by PAM rules to skip fingerprint
 * authentication when the lid is closed (e.g., when using external monitors).
 *
 * Exit codes (PAM-compatible):
 *   0 - Lid is CLOSED (PAM success - skip fingerprint)
 *   1 - Lid is OPEN or error (PAM ignore - proceed with fingerprint)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "dev-" __DATE__
#endif

#define PROGRAM_NAME "check-lid"
#define DBUS_TIMEOUT_USEC (5 * 1000000) /* 5 seconds in microseconds */

static int verbose = 0;

/**
 * @brief Signal handler for graceful termination.
 *
 * Exits immediately with fail-safe return code to proceed with normal auth.
 * Uses _exit() which is async-signal-safe.
 *
 * @param sig Signal number received (unused)
 */
__attribute__((noreturn)) static void signal_handler(int sig) {
    (void)sig; /* Unused parameter */
    /* Fail-safe: exit with code 1 to proceed with normal auth */
    _exit(EXIT_FAILURE);
}

/**
 * @brief Set up signal handlers for graceful termination.
 */
static void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

/**
 * @brief Log a message to syslog (and stderr in verbose mode).
 *
 * @param priority Syslog priority (LOG_INFO, LOG_ERR, etc.)
 * @param fmt Printf-style format string
 */
__attribute__((format(printf, 2, 3))) static void log_message(int priority, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if (verbose) {
        va_list args_copy;
        va_copy(args_copy, args);
        vfprintf(stderr, fmt, args_copy);
        fprintf(stderr, "\n");
        va_end(args_copy);
    }

    vsyslog(priority, fmt, args);
    va_end(args);
}

/**
 * @brief Print usage information.
 */
static void print_usage(void) {
    printf("Usage: %s [OPTIONS]\n\n", PROGRAM_NAME);
    printf("Check if the laptop lid is closed via systemd-logind D-Bus API.\n\n");
    printf("Options:\n");
    printf("  -v, --verbose    Enable verbose output (logs to stderr and syslog)\n");
    printf("  -V, --version    Show version information\n");
    printf("  -h, --help       Show this help message\n\n");
    printf("Exit codes:\n");
    printf("  0  Lid is CLOSED (PAM success - skip fingerprint)\n");
    printf("  1  Lid is OPEN or error occurred (PAM ignore - proceed normally)\n\n");
    printf("For PAM integration, see: https://github.com/shini4i/pam-lid-block\n");
}

/**
 * @brief Print version information.
 */
static void print_version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

/**
 * @brief Check for test mode environment variables.
 *
 * Only available when compiled with -DENABLE_TEST_MODE.
 * This is never active in production builds for security reasons.
 *
 * Supports error injection for testing fail-safe behavior.
 * Environment variables (for testing only):
 *   CHECK_LID_TEST_FORCE_ERROR=dbus    - Simulate D-Bus connection failure
 *   CHECK_LID_TEST_FORCE_ERROR=query   - Simulate property query failure
 *   CHECK_LID_TEST_FORCE_STATE=closed  - Force lid closed response
 *   CHECK_LID_TEST_FORCE_STATE=open    - Force lid open response
 *   CHECK_LID_TEST_DELAY_MS=<ms>       - Delay before returning (for signal testing)
 *
 * @param[out] forced_state Set to 0 (open), 1 (closed), or -1 (error) if forced
 * @return 1 if state was forced, 0 otherwise
 */
#ifdef ENABLE_TEST_MODE
static int check_test_mode(int *forced_state) {
    const char *force_error = getenv("CHECK_LID_TEST_FORCE_ERROR");
    const char *force_state = getenv("CHECK_LID_TEST_FORCE_STATE");
    const char *delay_ms = getenv("CHECK_LID_TEST_DELAY_MS");

    /* Apply delay if requested (for signal handler testing) */
    if (delay_ms != NULL) {
        char *endptr;
        long ms = strtol(delay_ms, &endptr, 10);
        if (endptr != delay_ms && *endptr == '\0' && ms > 0 && ms < 10000) {
            log_message(LOG_WARNING, "TEST MODE: Delaying %ld ms", ms);
            usleep((useconds_t)(ms * 1000));
        }
    }

    if (force_error != NULL) {
        if (strcmp(force_error, "dbus") == 0) {
            log_message(LOG_WARNING, "TEST MODE: Simulating D-Bus connection failure");
            *forced_state = -1;
            return 1;
        }
        if (strcmp(force_error, "query") == 0) {
            log_message(LOG_WARNING, "TEST MODE: Simulating property query failure");
            *forced_state = -1;
            return 1;
        }
    }

    if (force_state != NULL) {
        if (strcmp(force_state, "closed") == 0) {
            log_message(LOG_WARNING, "TEST MODE: Forcing lid state to CLOSED");
            *forced_state = 1;
            return 1;
        }
        if (strcmp(force_state, "open") == 0) {
            log_message(LOG_WARNING, "TEST MODE: Forcing lid state to OPEN");
            *forced_state = 0;
            return 1;
        }
    }

    return 0;
}
#else
static inline int check_test_mode(int *forced_state) {
    (void)forced_state;
    return 0; /* Test mode disabled in production builds */
}
#endif

/**
 * @brief Query the lid state from systemd-logind via D-Bus.
 *
 * Connects to the system D-Bus and queries the LidClosed property from
 * systemd-logind. Uses a timeout to prevent blocking PAM indefinitely.
 *
 * @return 1 if lid is closed, 0 if lid is open, -1 on error
 */
static int query_lid_state(void) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus *bus = NULL;
    int ret;
    int lid_closed = 0;

    /* Check for test mode injection (disabled in production builds) */
    int forced_state;
    /* cppcheck-suppress knownConditionTrueFalse */
    if (check_test_mode(&forced_state)) {
        return forced_state;
    }

    /* Connect to system bus */
    ret = sd_bus_open_system(&bus);
    if (ret < 0) {
        log_message(LOG_ERR, "Failed to connect to system bus: %s", strerror(-ret));
        return -1;
    }

    /* Set timeout to prevent blocking PAM indefinitely */
    ret = sd_bus_set_method_call_timeout(bus, DBUS_TIMEOUT_USEC);
    if (ret < 0) {
        log_message(LOG_WARNING, "Failed to set D-Bus timeout: %s", strerror(-ret));
        /* Continue anyway - timeout is a nice-to-have, not critical */
    }

    /* Get LidClosed property from logind */
    ret = sd_bus_get_property_trivial(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                      "org.freedesktop.login1.Manager", "LidClosed", &error, 'b',
                                      &lid_closed);

    if (ret < 0) {
        log_message(LOG_ERR, "Failed to query LidClosed property: %s",
                    error.message ? error.message : strerror(-ret));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return -1;
    }

    /* Clean up resources */
    sd_bus_error_free(&error);
    sd_bus_unref(bus);

    log_message(LOG_INFO, "Lid state: %s", lid_closed ? "CLOSED" : "OPEN");
    return lid_closed;
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {{"verbose", no_argument, 0, 'v'},
                                           {"help", no_argument, 0, 'h'},
                                           {"version", no_argument, 0, 'V'},
                                           {0, 0, 0, 0}};

    int opt;

    /* Set up signal handlers early for graceful termination */
    setup_signal_handlers();

    /* Open syslog connection before any potential logging */
    openlog(PROGRAM_NAME, LOG_PID, LOG_AUTH);

    while ((opt = getopt_long(argc, argv, "vVh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage();
                closelog();
                return EXIT_SUCCESS;
            case 'V':
                print_version();
                closelog();
                return EXIT_SUCCESS;
            default:
                print_usage();
                closelog();
                return EXIT_FAILURE;
        }
    }

    int lid_state = query_lid_state();

    closelog();

    if (lid_state < 0) {
        /* Error occurred - return failure to proceed with normal auth */
        return EXIT_FAILURE;
    }

    /* PAM logic: return success (0) if lid closed, failure (1) if open */
    return lid_state ? EXIT_SUCCESS : EXIT_FAILURE;
}
