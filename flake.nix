{
  description = "PlatformIO development shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        name = "platformio-shell";

        buildInputs = with pkgs; [
          platformio
          python3
          git
          gcc
          python3Packages.cryptography
        ];
      };
    };
}
