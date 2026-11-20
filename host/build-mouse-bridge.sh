#!/bin/sh
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir="$host_dir/ClawDock.app"
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Resources"
cp "$host_dir/Info.plist" "$app_dir/Contents/Info.plist"
cp "$host_dir/Resources/ClawDock.icns" "$app_dir/Contents/Resources/ClawDock.icns"
swiftc -O -target "$(uname -m)-apple-macos12.0" "$host_dir"/Sources/*.swift -framework ApplicationServices -framework AppKit -framework IOKit -o "$app_dir/Contents/MacOS/ClawDock"
codesign --force --sign - "$app_dir"
echo "已构建：$app_dir"
