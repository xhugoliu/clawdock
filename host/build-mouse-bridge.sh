#!/bin/sh
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir="$host_dir/JintaiMouse.app"
mkdir -p "$app_dir/Contents/MacOS"
cp "$host_dir/Info.plist" "$app_dir/Contents/Info.plist"
swiftc -O "$host_dir/mouse_bridge.swift" -framework ApplicationServices -o "$app_dir/Contents/MacOS/JintaiMouse"
codesign --force --sign - "$app_dir"
