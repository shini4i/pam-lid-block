# PAM Integration Guide

This guide explains how to integrate `check-lid` with PAM on various Linux distributions.

## Table of Contents

- [Understanding the PAM Rule](#understanding-the-pam-rule)
- [NixOS](#nixos)
- [Arch Linux](#arch-linux)
- [Debian/Ubuntu](#debianubuntu)
- [Fedora](#fedora)
- [Troubleshooting](#troubleshooting)

## Understanding the PAM Rule

The core PAM rule pattern is:

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /path/to/check-lid
auth    sufficient                     pam_fprintd.so
```

**How it works:**

1. `pam_exec.so` runs `check-lid`
2. If `check-lid` returns 0 (lid closed): `[success=1]` skips the next line (fingerprint)
3. If `check-lid` returns 1 (lid open): `[default=ignore]` continues to fingerprint

See [PAM-CONTROL-FLAGS.md](PAM-CONTROL-FLAGS.md) for detailed explanation.

## NixOS

The `pam-lid-block` package is available via [shini4i/nixpkgs](https://github.com/shini4i/nixpkgs).

### 1. Add flake input

```nix
# flake.nix
inputs.shini4i-pkgs.url = "github:shini4i/nixpkgs";
```

### 2. Add to overlay

```nix
# flake.nix (in outputs)
overlays.default = final: prev: {
  inherit (inputs.shini4i-pkgs.packages.${prev.stdenv.hostPlatform.system}) pam-lid-block;
};
```

### 3. Configure PAM

```nix
# configuration.nix
{ config, pkgs, lib, ... }:

let
  lidCheckBinary = "${pkgs.pam-lid-block}/bin/check-lid";

  # Helper to create lid check rule with proper ordering relative to fprintd
  mkLidCheckRule = service: {
    order = config.security.pam.services.${service}.rules.auth.fprintd.order - 10;
    control = "[success=1 default=ignore]";
    modulePath = "${pkgs.pam}/lib/security/pam_exec.so";
    args = ["quiet" lidCheckBinary];
  };
in
{
  services.fprintd.enable = true;

  security.pam.services = {
    sudo = {
      fprintAuth = true;
      rules.auth.lid-check = mkLidCheckRule "sudo";
    };

    polkit-1 = {
      fprintAuth = true;
      rules.auth.lid-check = mkLidCheckRule "polkit-1";
    };

    # GDM fingerprint requires [success=die] instead of [success=1] because
    # it's fingerprint-only (no pam_unix.so fallback). When lid is closed,
    # authentication must fail entirely so GDM falls back to gdm-password.
    gdm-fingerprint = lib.mkIf config.services.fprintd.enable {
      text = lib.mkForce ''
        auth      required                     pam_shells.so
        auth      requisite                    pam_nologin.so
        auth      requisite                    pam_faillock.so preauth
        auth      [success=die default=ignore] ${pkgs.pam}/lib/security/pam_exec.so quiet ${lidCheckBinary}
        auth      required                     ${pkgs.fprintd}/lib/security/pam_fprintd.so
        auth      required                     pam_env.so conffile=/etc/pam/environment readenv=0
        auth      [success=ok default=1]       ${pkgs.gdm}/lib/security/pam_gdm.so
        auth      optional                     ${pkgs.gnome-keyring}/lib/security/pam_gnome_keyring.so

        account   include                      login

        password  required                     pam_deny.so

        session   include                      login
        session   optional                     ${pkgs.gnome-keyring}/lib/security/pam_gnome_keyring.so auto_start
      '';
    };
  };
}
```

## Arch Linux

### Installation

```bash
# Install dependencies
sudo pacman -S gcc pkgconf systemd go-task

# Build from source
git clone https://github.com/shini4i/pam-lid-block
cd pam-lid-block
task build
sudo task install
```

### Configuration

Edit `/etc/pam.d/sudo`:

```pam
#%PAM-1.0
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    required                       pam_unix.so try_first_pass nullok
auth    optional                       pam_permit.so
auth    required                       pam_env.so

account required    pam_unix.so
account optional    pam_permit.so
account required    pam_time.so

session required    pam_limits.so
session required    pam_unix.so
session optional    pam_permit.so
session optional    pam_env.so
```

## Debian/Ubuntu

### Installation

```bash
# Install dependencies
sudo apt install build-essential pkg-config libsystemd-dev

# Install go-task (see https://taskfile.dev/installation/)
sh -c "$(curl --location https://taskfile.dev/install.sh)" -- -d -b /usr/local/bin

# Build from source
git clone https://github.com/shini4i/pam-lid-block
cd pam-lid-block
task build
sudo task install
```

### Configuration

Edit `/etc/pam.d/sudo`:

```pam
#%PAM-1.0
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
@include common-auth
@include common-account
@include common-session-noninteractive
```

## Fedora

### Installation

```bash
# Install dependencies
sudo dnf install gcc pkg-config systemd-devel

# Install go-task (see https://taskfile.dev/installation/)
sh -c "$(curl --location https://taskfile.dev/install.sh)" -- -d -b /usr/local/bin

# Build from source
git clone https://github.com/shini4i/pam-lid-block
cd pam-lid-block
task build
sudo task install
```

### Configuration

Edit `/etc/pam.d/sudo`:

```pam
#%PAM-1.0
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    include                        system-auth
account include                        system-auth
password include                       system-auth
session include                        system-auth
```

## Service-Specific Configurations

### GDM (GNOME Display Manager)

Edit `/etc/pam.d/gdm-fingerprint`:

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    required                       pam_deny.so
```

### SDDM (KDE Display Manager)

Edit `/etc/pam.d/sddm`:

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    include                        system-login
```

### swaylock

Edit `/etc/pam.d/swaylock`:

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    include                        login
```

### polkit

Edit `/etc/pam.d/polkit-1`:

```pam
auth    [success=1 default=ignore]    pam_exec.so quiet /usr/local/bin/check-lid
auth    sufficient                     pam_fprintd.so
auth    include                        system-auth
account include                        system-auth
password include                       system-auth
session include                        system-auth
```

## Troubleshooting

### Check if check-lid works

```bash
# Test manually
check-lid --verbose
echo "Exit code: $?"
```

### Check syslog for errors

```bash
# View recent check-lid logs
journalctl -t check-lid --since "10 minutes ago"
```

### Common Issues

**"Failed to connect to system bus"**
- Ensure systemd is running
- Check D-Bus permissions

**"Failed to query LidClosed property"**
- Ensure systemd-logind is running: `systemctl status systemd-logind`
- Some systems (desktops without a physical lid) may not have this property

**Fingerprint still times out**
- Verify PAM rule order (check-lid must come before pam_fprintd.so)
- Ensure `quiet` flag is used with pam_exec.so
- Check that the binary path is correct

**PAM rule has no effect**
- Verify the rule syntax with `pamtester` if available
- Check `/var/log/auth.log` or `journalctl -u systemd-logind`
