#!/bin/sh
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir="$host_dir/ClawDock.app"
mkdir -p "$app_dir/Contents/MacOS"
cp "$host_dir/Info.plist" "$app_dir/Contents/Info.plist"
swiftc -O -target "$(uname -m)-apple-macos12.0" "$host_dir"/Sources/*.swift -framework ApplicationServices -framework AppKit -framework IOKit -o "$app_dir/Contents/MacOS/ClawDock"
codesign --force --sign - "$app_dir"
echo "已构建：$app_dir"
