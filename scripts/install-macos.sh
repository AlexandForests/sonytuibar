#!/bin/sh
# Install the TUI on macOS:
#   - CLI:  /usr/local/bin/sonytui
#   - App:  ~/Applications/Sony Headphones TUI.app (opens Terminal running the TUI)
#
# Bluetooth (and, with the visualizer, audio-capture) permissions attach to the
# terminal app, not this bundle — the .app is just a launcher.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-release/client-tui/sonytui"
VERSION="$(sed -n 's/set(CLIENT_TUI_VERSION "\(.*\)")/\1/p' "$ROOT/client-tui/CMakeLists.txt")"

if [ ! -x "$BIN" ]; then
    echo "Release binary not found. Build it first:" >&2
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
mkdir -p "$APP/Contents/MacOS"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Sony Headphones TUI</string>
    <key>CFBundleIdentifier</key><string>local.sonyheadphones.tui</string>
    <key>CFBundleExecutable</key><string>launcher</string>
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

echo "Done."
echo "  - run 'sonytui' from any terminal"
echo "  - or launch 'Sony Headphones TUI' from ~/Applications (Spotlight finds it too)"
