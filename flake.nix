{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };
  outputs =
    { self, nixpkgs, ... }:
    let
      forAllSystems =
        function:
        nixpkgs.lib.genAttrs [
          "x86_64-linux"
          "aarch64-linux"
        ] (system: function (import nixpkgs { inherit system; }));

      # version = (builtins.readFile ./CMakeLists.txt).project.version;

      mkDate =
        longDate:
        (nixpkgs.lib.concatStringsSep "-" [
          (builtins.substring 0 4 longDate)
          (builtins.substring 4 2 longDate)
          (builtins.substring 6 2 longDate)
        ]);
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          name = "btrfs-assistant-shell";
          inherit (pkgs.btrfs-assistant) buildInputs propagatedBuildInputs;

          nativeBuildInputs =
            pkgs.btrfs-assistant.nativeBuildInputs
            ++ (with pkgs; [
              clang-tools
              gdb
            ]);

          shellHook = ''
            echo "btrfs-assistant-shell entered"
          '';
        };
      });

      overlays.default = final: prev: {
        btrfs-assistant = final.callPackage ./nix/default.nix {
          version =
            # TODO: parse version
            # version
            "+date=" + (mkDate (self.lastModifiedDate or "19700101")) + "_" + (self.shortRev or "dirty");
        };
      };

      packages = forAllSystems (
        pkgs:
        let
          packages = self.overlays.default pkgs pkgs;
        in
        packages // { default = packages.btrfs-assistant; }
      );
    };
}
