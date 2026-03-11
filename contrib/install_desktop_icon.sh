#!/bin/bash

# Get the absolute path of the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/src/qt"

# Define paths
ICON_PATH="$PROJECT_ROOT/src/qt/res/icons/bitcoin.png"
EXEC_PATH="$BUILD_DIR/bitfinite-qt"
DESKTOP_DIR="$HOME/.local/share/applications"
DESKTOP_FILE_PATH="$DESKTOP_DIR/bitfinite-core.desktop"

# Ensure desktop application directory exists
mkdir -p "$DESKTOP_DIR"

# Check if the icon and executable exist
if [ ! -f "$ICON_PATH" ]; then
    echo "Error: Icon not found at $ICON_PATH"
    exit 1
fi

if [ ! -f "$EXEC_PATH" ]; then
    echo "Warning: Executable not found at $EXEC_PATH. Have you compiled yet?"
    # Continue anyway so the desktop file is created
fi

echo "Creating desktop entry..."

cat > "$DESKTOP_FILE_PATH" <<EOF
[Desktop Entry]
Version=1.0
Name=Bitfinite Core
Comment=Connect to the Bitfinite P2P Network
Exec=$EXEC_PATH %u
Terminal=false
Type=Application
Icon=$ICON_PATH
MimeType=x-scheme-handler/bitfinite;
Categories=Office;Finance;P2P;Network;Qt;
StartupWMClass=Bitfinite-Qt
EOF

chmod +x "$DESKTOP_FILE_PATH"

echo "Success! BitFinite Core has been added to your application menu."
echo "You may need to log out and back in for the icon to appear in some menus."
