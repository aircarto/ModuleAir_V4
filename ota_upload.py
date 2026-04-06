"""
OTA Upload Script for ModuleAir
Uploads firmware.bin to the OTA server after a successful build.

Usage:
  Manual:     python ota_upload.py -d ModuleAir -v 0.0.1 -k YOUR_API_KEY
  PlatformIO: add "extra_scripts = post:ota_upload.py" to platformio.ini
              and configure ota_config.ini
"""

import sys
import os
import configparser
import argparse

# Detect if running as PlatformIO post-script or standalone
is_pio = "Import" in dir() if False else False
try:
    Import("env")
    is_pio = True
except:
    is_pio = False

FIRMWARE_PATH = os.path.join(".pio", "build", "esp32dev", "firmware.bin")
CONFIG_FILE = "ota_config.ini"
DEFAULT_SERVER = "https://gestion.aircarto.fr/api"
DEFAULT_DEVICE = "ModuleAir"


def get_version_from_config_h():
    """Extract FIRMWARE_VERSION from src/config.h"""
    config_h = os.path.join("src", "config.h")
    if os.path.exists(config_h):
        with open(config_h, "r") as f:
            for line in f:
                if '#define FIRMWARE_VERSION' in line:
                    return line.split('"')[1]
    return None


def upload_firmware(server, api_key, device, version, firmware_path):
    """Upload firmware to OTA server"""
    import requests

    url = f"{server}/ota/{device}/upload"

    print(f"[OTA] Uploading firmware v{version} for {device} to {url}")
    print(f"[OTA] File: {firmware_path} ({os.path.getsize(firmware_path)} bytes)")

    headers = {"X-OTA-Key": api_key}

    with open(firmware_path, "rb") as f:
        response = requests.post(
            url,
            files={"firmware": ("firmware.bin", f, "application/octet-stream")},
            data={"version": version, "device": device},
            headers=headers,
        )

    if response.status_code == 200:
        print(f"[OTA] Upload successful! Server response: {response.text}")
        return True
    else:
        print(f"[OTA] Upload failed! HTTP {response.status_code}: {response.text}")
        return False


def main():
    # Determine config source
    config = configparser.ConfigParser()
    server = DEFAULT_SERVER
    api_key = None
    device = None
    version = None

    # Try ota_config.ini first
    if os.path.exists(CONFIG_FILE):
        config.read(CONFIG_FILE)
        server = config.get("ota", "server", fallback=DEFAULT_SERVER)
        api_key = config.get("ota", "api_key", fallback=None)
        device = config.get("ota", "device", fallback=DEFAULT_DEVICE)
        version = config.get("ota", "version", fallback=None)

    # CLI arguments override config file
    parser = argparse.ArgumentParser(description="Upload OTA firmware")
    parser.add_argument("-d", "--device", help="Device type (e.g. ModuleAir)")
    parser.add_argument("-v", "--version", help="Firmware version")
    parser.add_argument("-k", "--key", help="API key")
    parser.add_argument("-s", "--server", help="Server URL")
    parser.add_argument("-f", "--firmware", help="Firmware binary path", default=FIRMWARE_PATH)

    args, _ = parser.parse_known_args()

    if args.device:
        device = args.device
    if args.version:
        version = args.version
    if args.key:
        api_key = args.key
    if args.server:
        server = args.server

    # Auto-detect version from config.h if not specified
    if not version:
        version = get_version_from_config_h()
        if version:
            print(f"[OTA] Auto-detected version from config.h: {version}")

    # Default device
    if not device:
        device = DEFAULT_DEVICE

    firmware_path = args.firmware

    # Validate
    if not api_key:
        print("[OTA] Error: API key required. Use -k flag or set api_key in ota_config.ini")
        sys.exit(1)
    if not version:
        print("[OTA] Error: Version required. Use -v flag or set version in ota_config.ini")
        sys.exit(1)
    if not os.path.exists(firmware_path):
        print(f"[OTA] Error: Firmware not found at {firmware_path}")
        sys.exit(1)

    upload_firmware(server, api_key, device, version, firmware_path)


# PlatformIO post-build hook
if is_pio:
    def ota_post_build(source, target, env):
        main()

    env.AddPostAction("$BUILD_DIR/firmware.bin", ota_post_build)
else:
    if __name__ == "__main__":
        main()
