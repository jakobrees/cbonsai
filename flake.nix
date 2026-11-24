{
  description = "cbonsai: grow bonsai trees in your terminal (jakobrees fork)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAll (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "cbonsai";
            version = "2.0.1";
            src = self;

            strictDeps = true;
            nativeBuildInputs = [ pkgs.pkg-config pkgs.scdoc ];
            buildInputs = [ pkgs.ncurses ];

            makeFlags = [
              "CC=${pkgs.stdenv.cc.targetPrefix}cc"
              "PREFIX=${placeholder "out"}"
            ];

            meta = {
              description = "Grow bonsai trees in your terminal, with seasonal color and real-time growth";
              homepage = "https://github.com/jakobrees/cbonsai";
              license = pkgs.lib.licenses.gpl3Only;
              mainProgram = "cbonsai";
              platforms = pkgs.lib.platforms.unix;
            };
          };
        });
    };
}
