#!/bin/sh
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app="$host_dir/JintaiMouse.app/Contents/MacOS/JintaiMouse"
if [ ! -x "$app" ] || [ "$host_dir/mouse_bridge.swift" -nt "$app" ] || [ "$host_dir/Info.plist" -nt "$app" ]; then
    "$host_dir/build-mouse-bridge.sh"
fi
port=${1:-}
if [ -z "$port" ]; then
    for candidate in /dev/cu.usbmodem* /dev/cu.usbserial*; do
        [ -e "$candidate" ] || continue
        if [ -n "$port" ]; then
            echo '发现多个串口，请将设备路径作为参数提供。' >&2
            exit 1
        fi
        port=$candidate
    done
fi
if [ -z "$port" ] || [ ! -e "$port" ]; then echo '未找到设备串口。' >&2; exit 1; fi
if lsof "$port" >/dev/null 2>&1; then echo "串口已被占用：$port" >&2; exit 1; fi
nohup "$app" "$port" > /tmp/jintai-mouse-bridge.log 2>&1 < /dev/null &
bridge_pid=$!
sleep 1
if kill -0 "$bridge_pid" 2>/dev/null; then
    echo "鼠标桥接已启动：${port}（进程 ${bridge_pid}，日志 /tmp/jintai-mouse-bridge.log）"
else
    cat /tmp/jintai-mouse-bridge.log >&2
    exit 1
fi
