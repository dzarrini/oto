{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
  };

  outputs = { self, nixpkgs }: {
    packages.x86_64-linux.default =
      with import nixpkgs { system = "x86_64-linux"; };
      stdenv.mkDerivation {
        name = "oto";
        src = self;
        nativeBuildInputs = [ pkg-config ];
        buildInputs = [ ncurses fftw pipewire.dev];
        installPhase = ''
          runHook preInstall
          mkdir -p $out/bin
          cp oto $out/bin
          runHook postInstall
        '';
      };
  };
}
