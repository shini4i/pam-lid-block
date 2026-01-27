# Understanding PAM Control Flags

This document explains how PAM control flags work and why `check-lid` uses the pattern it does.

## PAM Basics

PAM (Pluggable Authentication Modules) processes authentication in a stack of modules. Each module returns a result, and the control flag determines what happens next.

## Control Flag Syntax

Modern PAM uses bracket notation for fine-grained control:

```
[value1=action1 value2=action2 ...]
```

### Common Values (Module Results)

| Value | Meaning |
|-------|---------|
| `success` | Module succeeded (returned PAM_SUCCESS) |
| `default` | Any result not explicitly listed |
| `ignore` | Module returned PAM_IGNORE |
| `new_authtok_reqd` | Password needs to be changed |
| `user_unknown` | User not found |
| `auth_err` | Authentication error |

### Common Actions

| Action | Meaning |
|--------|---------|
| `ok` | Continue to next module, this result is acceptable |
| `done` | Stop processing, return current result |
| `die` | Stop processing, return failure |
| `ignore` | Ignore this module's result |
| `bad` | Mark this as a failure but continue |
| `N` (number) | Skip the next N modules |

## How check-lid Uses PAM

### The Rule

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /path/to/check-lid
auth    sufficient                     pam_fprintd.so
auth    required                       pam_unix.so
```

### Flow Analysis

**Case 1: Lid is CLOSED (check-lid returns 0 = PAM_SUCCESS)**

1. `pam_exec.so` runs `check-lid`, which returns 0
2. `[success=1]` → Skip 1 module (skip fingerprint)
3. Control jumps to `pam_unix.so` (password auth)
4. Result: Fingerprint is skipped, password is requested

**Case 2: Lid is OPEN (check-lid returns 1 = PAM_AUTH_ERR)**

1. `pam_exec.so` runs `check-lid`, which returns 1
2. `[default=ignore]` → Ignore this result, continue
3. `pam_fprintd.so` is executed (fingerprint auth)
4. Result: Fingerprint authentication proceeds normally

### Why This Works

The key insight is that PAM exit codes are inverted from shell conventions:
- Shell: 0 = success, non-zero = failure
- PAM: 0 = PAM_SUCCESS, 1 = PAM_AUTH_ERR

`check-lid` returns:
- 0 (PAM_SUCCESS) when lid is closed → trigger `[success=1]` to skip fingerprint
- 1 (PAM_AUTH_ERR) when lid is open → trigger `[default=ignore]` to continue

## Alternative Patterns

### Using `[success=die]`

```pam
auth    [success=die default=ignore]    pam_exec.so quiet /path/to/check-lid
auth    sufficient                       pam_fprintd.so
```

With `[success=die]`:
- Lid closed → Authentication succeeds immediately (no password needed)
- Lid open → Continue to fingerprint

**Use case:** When you want to skip ALL authentication if lid is closed.

### Using `sufficient`

```pam
auth    sufficient    pam_exec.so quiet /path/to/check-lid
auth    sufficient    pam_fprintd.so
auth    required      pam_unix.so
```

This is simpler but less flexible:
- Lid closed → `check-lid` succeeds, auth complete
- Lid open → `check-lid` fails, try fingerprint, then password

**Drawback:** If lid is closed, no password is required at all (security concern).

## Debugging PAM

### Test with pamtester

```bash
# Install pamtester (if available)
sudo pamtester sudo $USER authenticate
```

### Check logs

```bash
# View PAM authentication logs
journalctl -t sudo --since "5 minutes ago"
journalctl -t check-lid --since "5 minutes ago"
```

### Add debug output

Temporarily modify PAM rule to see what's happening:

```pam
auth    [success=1 default=ignore]    pam_exec.so /path/to/check-lid --verbose
```

Note: Removing `quiet` will show output to the user during authentication.

## Further Reading

- [Linux-PAM documentation](http://www.linux-pam.org/Linux-PAM-html/)
- [pam.conf(5) man page](https://man7.org/linux/man-pages/man5/pam.conf.5.html)
- [pam_exec(8) man page](https://man7.org/linux/man-pages/man8/pam_exec.8.html)
