#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Builds a Zigbee OTA Upgrade file (ZCL 11.4) from a signed MCUboot image."""

import argparse
import hashlib
import json
import os
import pathlib
import struct
import sys

MCUBOOT_MAGIC = struct.pack("<I", 0x96F3B83D)
OTA_IDENTIFIER = 0x0BEEF11E
HEADER_VERSION = 0x0100
HEADER_LEN = 56
ZIGBEE_PRO = 0x0002
UPGRADE_IMAGE_TAG = 0x0000
SUB_ELEMENT_HEADER_LEN = 6


def uint(maximum):
    def integer(text):
        value = int(text, 0)
        if not 0 <= value <= maximum:
            raise argparse.ArgumentTypeError(f"{text} is outside 0 to {maximum:#x}")
        return value

    return integer


def header_string(text):
    if not text.isascii() or len(text) > 31:
        raise argparse.ArgumentTypeError("at most 31 ASCII characters")
    return text.encode()


def add_to_index(args, ota_file):
    """Writes the entry of the OTA file into the index file of the zigpy_local provider."""
    index = json.loads(args.index.read_text()) if args.index.exists() else {"firmwares": []}
    image = (args.manufacturer, args.image_type, args.file_version)
    index["firmwares"] = [
        fw
        for fw in index["firmwares"]
        if (fw["manufacturer_id"], fw["image_type"], fw["file_version"]) != image
    ]
    index["firmwares"].append(
        {
            "path": os.path.relpath(args.output, args.index.parent),
            "file_version": args.file_version,
            "file_size": len(ota_file),
            "image_type": args.image_type,
            "manufacturer_id": args.manufacturer,
            "checksum": "sha3-256:" + hashlib.sha3_256(ota_file).hexdigest(),
        }
    )
    args.index.write_text(json.dumps(index, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manufacturer", type=uint(0xFFFE), required=True)
    parser.add_argument("--image-type", type=uint(0xFFBF), required=True)
    parser.add_argument("--file-version", type=uint(0xFFFFFFFF), required=True)
    parser.add_argument("--header-string", type=header_string, default=b"zzb")
    parser.add_argument("--index", type=pathlib.Path, help="index file of zigpy_local to update")
    parser.add_argument("image", type=argparse.FileType("rb"), help="zephyr.signed.bin")
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    image = args.image.read()
    if not image.startswith(MCUBOOT_MAGIC):
        sys.exit(f"{args.image.name} has no MCUboot image header; use zephyr.signed.bin")

    # The header string is NUL-terminated and padded with NUL to 32 octets.
    header = struct.pack(
        "<IHHHHHIH32sI",
        OTA_IDENTIFIER,
        HEADER_VERSION,
        HEADER_LEN,
        0,  # field control: no optional fields
        args.manufacturer,
        args.image_type,
        args.file_version,
        ZIGBEE_PRO,
        args.header_string,
        HEADER_LEN + SUB_ELEMENT_HEADER_LEN + len(image),
    )
    sub_element = struct.pack("<HI", UPGRADE_IMAGE_TAG, len(image))
    ota_file = header + sub_element + image
    args.output.write_bytes(ota_file)
    if args.index:
        add_to_index(args, ota_file)


if __name__ == "__main__":
    main()
