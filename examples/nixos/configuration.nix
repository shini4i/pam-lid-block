# Example NixOS configuration for pam-lid-block
#
# This example shows how to integrate check-lid with PAM services on NixOS.
# The package is available via shini4i/nixpkgs flake.

{ config, lib, pkgs, ... }:

let
  # Import check-lid from shini4i/nixpkgs
  # Add shini4i-nixpkgs as a flake input in your flake.nix:
  #
  # inputs.shini4i-nixpkgs.url = "github:shini4i/nixpkgs";
  #
  # Then pass it to your NixOS configuration and use:
  # check-lid = inputs.shini4i-nixpkgs.packages.${pkgs.system}.check-lid;

  # Placeholder - replace with actual package reference
  check-lid = pkgs.check-lid or (throw "check-lid package not found - add shini4i/nixpkgs to your flake inputs");
in
{
  # Enable fingerprint authentication
  services.fprintd.enable = true;

  # Add check-lid to system packages (optional, for manual testing)
  environment.systemPackages = [ check-lid ];

  # Configure PAM services to skip fingerprint when lid is closed
  security.pam.services = {
    # sudo
    sudo.text = lib.mkBefore ''
      auth [success=1 default=ignore] pam_exec.so quiet ${check-lid}/bin/check-lid
    '';

    # polkit (for GUI privilege escalation)
    polkit-1.text = lib.mkBefore ''
      auth [success=1 default=ignore] pam_exec.so quiet ${check-lid}/bin/check-lid
    '';

    # Login manager (uncomment as needed)
    # gdm-fingerprint.text = lib.mkBefore ''
    #   auth [success=1 default=ignore] pam_exec.so quiet ${check-lid}/bin/check-lid
    # '';

    # Screen locker (uncomment as needed)
    # swaylock.text = lib.mkBefore ''
    #   auth [success=1 default=ignore] pam_exec.so quiet ${check-lid}/bin/check-lid
    # '';
  };
}
