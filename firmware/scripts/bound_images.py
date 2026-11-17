"""Ensure PlatformIO-produced images carry the ESP32-P4 v3 revision bounds."""

Import("env")  # type: ignore[name-defined]  # supplied by PlatformIO

from pathlib import Path
import re


def bound_image(source, target, env):
    config_file = Path(env.subst("$PROJECT_DIR")) / ("sdkconfig." + env.subst("$PIOENV"))
    config = config_file.read_text(encoding="utf-8")
    bounds = []
    for key in ("MIN", "MAX"):
        match = re.search(rf"^CONFIG_ESP_REV_{key}_FULL=(\d+)$", config, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"Missing chip revision {key} in {config_file}")
        bounds.append(int(match.group(1)))
    if bounds != [300, 399]:
        raise RuntimeError(f"Expected P4 v3 revision bounds 300–399, got {bounds}")

    output = Path(str(target[0]))
    elf = output.with_suffix(".elf")
    app_arg = " --elf-sha256-offset 0xb0" if output.name == "firmware.bin" else ""
    command = (f'$ERASETOOL --chip esp32p4 elf2image --flash-mode dio --flash-freq 80m '
               f'--flash-size 32MB --min-rev-full 300 --max-rev-full 399{app_arg} '
               f'-o "{output}" "{elf}"')
    if env.Execute(command):
        raise RuntimeError(f"Failed to build revision-bounded {output.name}")
    header = output.read_bytes()[:24]
    if (header[0] != 0xe9 or int.from_bytes(header[12:14], "little") != 18
            or int.from_bytes(header[15:17], "little") != 300
            or int.from_bytes(header[17:19], "little") != 399):
        raise RuntimeError(f"Invalid ESP32-P4 v3 image header in {output}")

    # The automatic factory image may have been created using the unbounded app.
    # This lab only produces an app for investigation, not a flash-ready bundle.
    if output.name == "firmware.bin":
        output.with_name("firmware.factory.bin").unlink(missing_ok=True)


env.Depends("$BUILD_DIR/bootloader.bin", "$PROJECT_DIR/scripts/bound_images.py")
env.Depends("$BUILD_DIR/${PROGNAME}.bin", "$PROJECT_DIR/scripts/bound_images.py")
env.AddPostAction("$BUILD_DIR/bootloader.bin", bound_image)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", bound_image)
