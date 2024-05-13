{
  btrfs-progs,
  cmake,
  coreutils,
  git,
  lib,
  nix-update-script,
  pkg-config,
  qt6,
  snapper,
  stdenv,
  util-linux,
  version,
  enableSnapper ? true,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "btrfs-assistant";
  inherit version;

  src = lib.cleanSourceWith {
    filter = name: type: type != "regular" || !lib.hasSuffix ".nix" name;
    src = lib.cleanSource ../.;
  };

  nativeBuildInputs = [
    cmake
    git
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    btrfs-progs
    coreutils
    qt6.qtbase
    qt6.qtsvg
    qt6.qttools
    qt6.qtwayland
    util-linux
  ] ++ lib.optionals enableSnapper [ snapper ];

  prePatch = lib.optionalString enableSnapper ''
    substituteInPlace src/main.cpp \
      --replace-fail '/usr/bin/snapper' "${lib.getExe snapper}"
  '';

  postPatch =
    ''
      substituteInPlace src/org.btrfs-assistant.pkexec.policy \
        --replace-fail '/usr/bin' "$out/bin"

      substituteInPlace src/btrfs-assistant \
        --replace-fail 'btrfs-assistant-bin' "$out/bin/btrfs-assistant-bin"

      substituteInPlace src/btrfs-assistant-launcher \
        --replace-fail 'btrfs-assistant' "$out/bin/btrfs-assistant"
    ''
    + lib.optionalString enableSnapper ''
      substituteInPlace src/btrfs-assistant.conf \
        --replace-fail '/usr/bin/snapper' "${lib.getExe snapper}"
    '';

  passthru.updateScript = nix-update-script { };

  meta = {
    description = "A GUI management tool to make managing a Btrfs filesystem easier";
    homepage = "https://gitlab.com/btrfs-assistant/btrfs-assistant";
    license = lib.licenses.gpl3Only;
    mainProgram = "btrfs-assistant-bin";
    maintainers = with lib.maintainers; [ khaneliman ];
    platforms = lib.platforms.linux;
  };
})
