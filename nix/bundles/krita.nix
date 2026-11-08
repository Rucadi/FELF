# Krita as a FELF tree.  A stress test, not a minimal example.
#
# pkgs.krita.unwrapped/bin/krita is a Nix wrapper script; the real ELF is
# bin/.krita-wrapped.  The FELF replaces both with bin/krita.  The Qt plugin
# and QML directories are taken from that wrapper (exactly what nixpkgs
# would put on QT_PLUGIN_PATH), copied into the tree as plain files, and
# every library they need is embedded.
{
  pkgs,
  # lib/bundle.nix applied to the FELF tools.
  bundle,
}:
let
  # Python standard library for the tree, without the test suite and the
  # Tk-based tools.
  pythonStdlib = pkgs.runCommand "krita-python-stdlib" { } ''
    mkdir -p $out
    cp -a ${pkgs.python3}/lib/python${pkgs.python3.pythonVersion}/. $out/
    chmod -R u+w $out
    rm -rf $out/test $out/idlelib $out/turtledemo $out/tkinter
  '';
in
bundle {
  package = pkgs.krita.unwrapped;
  binary = "bin/.krita-wrapped";
  programName = "krita";
  # Krita's other executables share the same libraries: one FELF,
  # bin/kritarunner and bin/krita_version are symlinks to it.
  programs = {
    kritarunner = "bin/.kritarunner-wrapped";
    krita_version = "bin/.krita_version-wrapped";
  };
  # Host GL/Vulkan drivers must stay loadable; bundled names still win over
  # the host for everything that is embedded.
  strict = false;
  lazy = true;
  qtConf = true;
  qtPluginsFrom = [
    "${pkgs.krita.unwrapped}/bin/krita"
    "${pkgs.krita}/bin/krita"
  ];
  # Fallback if the wrapper format changes: always have qtbase.
  extraTrees = [
    {
      src = "${pkgs.qt6.qtbase}/lib/qt-6/plugins";
      dest = "lib/qt-6/plugins";
    }
    # Krita embeds libpython; without its standard library Python dies at
    # startup ("No module named 'encodings'") and takes Krita with it.
    # lib-dynload/*.so become tree plugins, so the libraries they need
    # (libffi, OpenSSL, ...) are embedded.
    {
      src = pythonStdlib;
      dest = "lib/python${pkgs.python3.pythonVersion}";
    }
  ];
  includes = [
    "${pkgs.sdl3.lib}/lib/libSDL3.so.0"
  ];
  env = {
    # Override the host: NixOS sets these globally and they would load host
    # Qt plugins built against a different Qt.
    QT_PLUGIN_PATH = "$FELF_ROOT/lib/qt-6/plugins";
    QT_QPA_PLATFORM_PLUGIN_PATH = "$FELF_ROOT/lib/qt-6/plugins/platforms";
    QML_IMPORT_PATH = "$FELF_ROOT/lib/qt-6/qml";
    QML2_IMPORT_PATH = "$FELF_ROOT/lib/qt-6/qml";
    NIXPKGS_QT6_QML_IMPORT_PATH = "$FELF_ROOT/lib/qt-6/qml";
    NIX_PROFILES = "";
    KRITA_PLUGIN_PATH = "$FELF_ROOT/lib/kritaplugins";
    PYTHONHOME = "$FELF_ROOT";
    PYTHONPATH = "$FELF_ROOT/lib/krita-python-libs";
    XDG_DATA_DIRS = "$FELF_ROOT/share:$XDG_DATA_DIRS:/usr/local/share:/usr/share";
  };
}
