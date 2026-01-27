{
  description = "Development environment for pam-lid-block";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            # Build tools
            gcc
            clang
            go-task
            pkg-config

            # Static analysis and formatting
            cppcheck
            clang-tools  # includes clang-format and clang-tidy
          ];

          buildInputs = with pkgs; [
            systemd
          ];

          shellHook = ''
            echo "pam-lid-block development shell"
            echo ""
            echo "Available tasks (run 'task --list' for full list):"
            echo "  task build      - Build the check-lid binary"
            echo "  task test       - Run unit tests"
            echo "  task lint       - Run static analysis"
            echo "  task format     - Format code"
            echo "  task ci         - Run all CI checks"
          '';
        };
      }
    );
}
