{
  description = "HPC project dev environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      # self,
      nixpkgs,
      # flake-utils,
      ...
    }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells."${system}".default = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          gnumake
          mpi
          (pkgs.python3.withPackages (ps: [
            ps.matplotlib
          ]))
        ];
      };
    };
}
