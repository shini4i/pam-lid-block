<h1 align="center">pam-lid-block</h1>

<p align="center">
  <img src="https://github.com/shini4i/pam-lid-block/actions/workflows/qa.yml/badge.svg" alt="QA">
  <img src="https://img.shields.io/github/license/shini4i/pam-lid-block" alt="GitHub license">
  <img src="https://img.shields.io/badge/platform-linux-blue" alt="Platform">
</p>

<p align="center">
  A PAM helper utility to skip fingerprint authentication when the laptop lid is closed.
</p>

## The Problem

When using a laptop in clamshell mode (lid closed, external monitor connected):
- Fingerprint scanners are physically inaccessible
- PAM authentication (sudo, polkit, screen lock) waits for fingerprint timeout
- This can block authentication for 10-20+ seconds

## How It Works

`check-lid` queries the `LidClosed` property from `systemd-logind` via D-Bus. When integrated with PAM, it skips fingerprint authentication when the lid is closed.

**Exit codes (PAM-compatible):**
- `0` - Lid is **CLOSED** → skip fingerprint
- `1` - Lid is **OPEN** or error → proceed with fingerprint

## Building

```bash
git clone https://github.com/shini4i/pam-lid-block
cd pam-lid-block

# Option 1: Using Nix
nix develop
task build

# Option 2: Manual
gcc -O2 -Wall -Wextra -fstack-protector-strong -fPIE \
    $(pkg-config --cflags libsystemd) \
    -o check-lid src/check-lid.c \
    -pie $(pkg-config --libs libsystemd)
```

**Dependencies:** `libsystemd`, `pkg-config`, `gcc`

## PAM Integration

Add `check-lid` to your PAM configuration:

```pam
# Skip fingerprint if lid is closed
auth    [success=1 default=ignore]    pam_exec.so quiet /path/to/check-lid
auth    sufficient                     pam_fprintd.so
auth    required                       pam_unix.so
```

See [INTEGRATION.md](docs/INTEGRATION.md) for distro-specific guides (NixOS, Arch, Debian, Fedora).

## Usage

```bash
check-lid && echo "Lid closed" || echo "Lid open"
check-lid --verbose  # Debug output
check-lid --help
```

## Why D-Bus?

| Approach | Pros | Cons |
|----------|------|------|
| **D-Bus** | Authoritative, consistent | Requires systemd |
| procfs | No dependencies | Path varies |
| sysfs | Direct hardware access | Path varies |

## Security

- Security-hardened build flags (stack protector, PIE, RELRO, FORTIFY_SOURCE)
- Runs as calling user (no privilege escalation)
- Only reads lid state via D-Bus
- Logs to syslog (LOG_AUTH facility)

## License

GPL-3.0 - see [LICENSE](LICENSE) for details.
