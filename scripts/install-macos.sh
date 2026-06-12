#!/bin/sh
# Install the macOS clients. Pick what you want:
#   ./scripts/install-macos.sh          # both (default)
#   ./scripts/install-macos.sh tui      # TUI only  (CLI + launcher .app)
#   ./scripts/install-macos.sh bar      # menu bar app only
#
# TUI install lays down:
#   - CLI:  /usr/local/bin/sonytui          (needs sudo if /usr/local/bin isn't writable)
#   - App:  ~/Applications/Sony Headphones TUI.app  (opens Terminal running the TUI)
#     Bluetooth / audio-capture permissions attach to the terminal, not this
#     bundle — the .app is just a launcher.
# Menu bar install lays down:
#   - App:  ~/Applications/Sony Headphones Bar.app  (real bundle, own Bluetooth permission)
set -e

MODE="${1:-both}"
case "$MODE" in
    both|tui|bar) ;;
    *) echo "Usage: $0 [both|tui|bar]" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/set(CLIENT_TUI_VERSION "\(.*\)")/\1/p' "$ROOT/client-tui/CMakeLists.txt")"

install_tui() {
    BIN="$ROOT/build-release/client-tui/sonytui"
    if [ ! -x "$BIN" ]; then
        echo "TUI release binary not found. Build it first:" >&2
        echo "  cmake -S \"$ROOT\" -B \"$ROOT/build-release\" -DCMAKE_BUILD_TYPE=Release -DMDR_ENABLE_CODEGEN=OFF -DMDR_BUILD_CLIENT=OFF" >&2
        echo "  cmake --build \"$ROOT/build-release\" --target SonyHeadphonesClientTUI" >&2
        exit 1
    fi

    # --- CLI on PATH ---
    DEST=/usr/local/bin/sonytui
    echo "Installing $DEST"
    if [ -w /usr/local/bin ]; then
        install -m 755 "$BIN" "$DEST"
    else
        sudo install -m 755 "$BIN" "$DEST"
    fi

    # --- .app bundle (per-user, no sudo) ---
    APP="$HOME/Applications/Sony Headphones TUI.app"
    echo "Assembling $APP"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
    cp "$ROOT/client-tui/resources/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"

    cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Sony Headphones TUI</string>
    <key>CFBundleIdentifier</key><string>local.sonyheadphones.tui</string>
    <key>CFBundleExecutable</key><string>launcher</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
</dict>
</plist>
EOF

    cat > "$APP/Contents/MacOS/launcher" <<'EOF'
#!/bin/sh
exec open -a Terminal /usr/local/bin/sonytui
EOF
    chmod +x "$APP/Contents/MacOS/launcher"
}

install_bar() {
    BAR_SRC="$ROOT/build-release/client-menubar/Sony Headphones Bar.app"
    if [ ! -d "$BAR_SRC" ]; then
        if [ "$MODE" = "both" ]; then
            echo "Menu bar app not built — skipping. To include it:" >&2
            echo "  cmake --build \"$ROOT/build-release\" --target SonyHeadphonesMenuBar" >&2
            return 0
        fi
        echo "Menu bar app not found. Build it first:" >&2
        echo "  cmake -S \"$ROOT\" -B \"$ROOT/build-release\" -DCMAKE_BUILD_TYPE=Release -DMDR_ENABLE_CODEGEN=OFF -DMDR_BUILD_CLIENT=OFF" >&2
        echo "  cmake --build \"$ROOT/build-release\" --target SonyHeadphonesMenuBar" >&2
        exit 1
    fi
    BAR_DEST="$HOME/Applications/Sony Headphones Bar.app"
    echo "Installing $BAR_DEST"
    rm -rf "$BAR_DEST"
    cp -R "$BAR_SRC" "$BAR_DEST"
}

case "$MODE" in tui|both) install_tui ;; esac
case "$MODE" in bar|both) install_bar ;; esac

echo "Done."
case "$MODE" in
    tui|both)
        echo "  - run 'sonytui' from any terminal"
        echo "  - or launch 'Sony Headphones TUI' from ~/Applications (Spotlight finds it too)" ;;
esac
case "$MODE" in
    bar|both)
        echo "  - menu bar app: launch 'Sony Headphones Bar' from ~/Applications" ;;
esac
