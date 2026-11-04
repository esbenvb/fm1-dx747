#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# A port of sdk/Package.cs (Copyright (c) 2023 Andrey Grigoryev (kagaimiq), 2026 M-VAVE; JieLi formats and
# ciphers from jl-misctools 0a5b12d; FM1 layout and writer by M-VAVE; full MIT terms in THIRD_PARTY.md) and of
# the image step of Package-Firmware in sdk/Build.ps1, so that the firmware package can be made on a machine
# that has no PowerShell or .NET, from an ELF file that any build produced.
"""Make the FM-1 firmware package (.fwsc) from a linked sdk.elf, the way sdk/Build.ps1 and sdk/Package.cs do.

  make_fwsc.py SDK.elf OUT.fwsc [--product FM-1_8747747] [--resources sdk/tools/pack]

The product defaults to FM1_UPDATE_PRODUCT in app/version.h. The result is checked against the Windows
build by comparing bytes (see docs/BUILD-WINDOWS.md, "Comparing a Windows build with the Docker build").
This is a check and a convenience, not the supported route: the SDK's own packager is Package.cs run by
Build.ps1 on Windows, and the file that gets flashed should come from there.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_report import Elf  # noqa: E402

AREA, ENTRY, APP_DATA = 0x4000, 0x02000120, 0x120
FLASH_LIMIT, USER_SIZE, BT_SIZE, MIN_VM = 0xFF000, 536576, 4096, 32768
PUBLIC_CODE = 0x980F


def make_crc_table():
    table = []
    for i in range(256):
        value = i << 8
        for _ in range(8):
            value = (value << 1) ^ (0x1021 if value & 0x8000 else 0)
        table.append(value & 0xFFFF)
    return table


CRC_TABLE = make_crc_table()


def crc(data, begin=0, length=None):
    length = len(data) - begin if length is None else length
    value = 0
    for b in data[begin:begin + length]:
        value = ((value << 8) ^ CRC_TABLE[(value >> 8) ^ b]) & 0xFFFF
    return value


def cipher(buf, begin, length, state):
    for i in range(begin, begin + length):
        buf[i] ^= state & 0xFF
        state = ((state << 1) ^ (0x1021 if state & 0x8000 else 0)) & 0xFFFF


def round_up(size, alignment):
    return (size + alignment - 1) & -alignment


class Image:
    def __init__(self, size, fill):
        self.b = bytearray([fill]) * size

    def number(self, offset, value, width):
        for n in range(width):
            self.b[offset + n] = (value >> (8 * n)) & 0xFF

    def copy(self, offset, data):
        self.b[offset:offset + len(data)] = data

    def text(self, offset, value):
        self.copy(offset, value.encode("ascii"))

    def checksum(self, field, offset, size):
        self.number(field, crc(self.b, offset, size), 2)

    def record(self, offset, name, crc_value, address, size, attributes, last, extra=None):
        self.b[offset:offset + 32] = bytes(32)
        self.number(offset + 2, crc_value, 2)
        self.number(offset + 4, address, 4)
        self.number(offset + 8, size, 4)
        self.number(offset + 12, attributes, 2)
        self.number(offset + 14, last, 2)
        for n in range(16, 32):
            self.b[offset + n] = 255
        self.text(offset + 16, name)
        self.b[offset + 16 + len(name)] = 0
        if extra is not None:
            self.number(offset + 28, extra, 4)
        self.checksum(offset, offset + 2, 30)


def read_resource(directory, name, size):
    data = open(os.path.join(directory, name), "rb").read()
    if len(data) != size:
        raise SystemExit("Unexpected FM1 resource: " + name)
    return data


def encode_boot(plain):
    result = bytearray(plain)
    banks = struct.unpack_from("<H", plain, 0)[0]
    if banks < 1 or banks > len(plain) // 16:
        raise SystemExit("Invalid boot directory.")
    previous_end = banks * 16
    for i in range(banks):
        header = i * 16
        length = struct.unpack_from("<H", plain, header + 2)[0]
        start = struct.unpack_from("<i", plain, header + 8)[0]
        if (length == 0 or start < previous_end or start > len(plain) - length or
                crc(plain, header, 14) != struct.unpack_from("<H", plain, header + 14)[0] or
                crc(plain, start, length) != struct.unpack_from("<H", plain, header + 12)[0]):
            raise SystemExit("Invalid boot bank.")
        cipher(result, header, 16, 65535)
        cipher(result, start, length, 65535)
        previous_end = start + length
    return bytes(result)


def build_flash(application, directory, end, vm_end):
    boot = read_resource(directory, "uboot.boot", 14488)
    config = read_resource(directory, "boot-config.bin", 721)
    tool = read_resource(directory, "cfg_tool.bin", 383)
    image = Image(end, 255)
    settings = Image(34 + len(config), 0)
    for bit in range(16):
        settings.b[bit] = 16
        settings.b[16 + bit] = 16 ^ (255 if ((PUBLIC_CODE >> bit) & 1) == 0 else 0)
    settings.checksum(32, 0, 32)
    settings.copy(34, config)
    image.copy(0, bytes([0, 0, 0x20, 2, 0x2f, 0x10, 0x4c, 0xc9, 0, 0xf0, 0x0f, 0, 0x10, 0x10, 0xa4, 0xff,
                         0x4e, 0x5d, 0x0b, 0x41, 0xe0, 0xcd, 0x78, 0x1d, 0xc8, 0x56, 0x41, 0x7f, 0xb3, 0x67, 0xcf, 0x9f]))
    image.checksum(0, 2, 30)
    config_at = 160 + len(boot)
    image.record(32, "uboot.boot", crc(boot), 160, len(boot), 0, 0)
    image.record(64, "isd_config.ini", crc(settings.b), config_at, len(settings.b), 0x8002, 0)
    image.record(96, "app_dir_head", 65535, AREA, 0xFFFFFFFF, 0xff81, 0)
    image.record(128, "key_mac", 65535, FLASH_LIMIT, 4096, 0x0112, 65535)
    for p in range(0, 160, 32):
        cipher(image.b, p, 32, 65535)
    image.copy(160, encode_boot(boot))
    image.copy(config_at, settings.b)
    payload_size = APP_DATA + len(application) + len(tool)
    app = AREA + APP_DATA
    image.copy(app, application)
    image.copy(app + len(application), tool)
    image.record(AREA + 32, "app.bin", crc(application), APP_DATA, len(application), 0xff82, 0)
    image.record(AREA + 64, "cfg_tool.bin", crc(tool), APP_DATA + len(application), len(tool), 0xff82, 0)
    image.record(AREA + 96, "VM", 65535, end, vm_end - end, 0x8112, 0, MIN_VM)
    image.record(AREA + 128, "PRCT", 65535, 0, end, 0x8292, 0, 0x45444f43)
    image.record(AREA + 160, "BTIF", 65535, vm_end, BT_SIZE, 0x8192, 1, 0x4f545541)
    image.record(AREA, "app_area_head", crc(image.b, AREA + 32, payload_size - 32), ENTRY, payload_size, 0xff83, 0)
    reserved = AREA + payload_size
    image.record(reserved + 32, "USER", 65535, FLASH_LIMIT - USER_SIZE, USER_SIZE, 0x8192, 1, 0x4f545541)
    image.record(reserved, "EXT_RESERVED", crc(image.b, reserved + 32, 32), 32, 64, 0xff93, 1)
    for p in range(0, payload_size + 64, 32):
        cipher(image.b, AREA + p, min(32, payload_size + 64 - p), PUBLIC_CODE ^ (p >> 2))
    return image


def wrap(flash, loader, product):
    if len(loader) < 1 or len(loader) >= 32768:
        raise SystemExit("Invalid FM1 update loader.")
    loader_at = 1024 + round_up(len(flash.b), 32)
    total = loader_at + round_up(len(loader), 32)
    ufw = Image(total, 255)
    ufw.b[0:224] = bytes(224)
    files = [bytes(flash.b), loader]
    names = ["flash.bin", "ota.bin"]
    for i, data in enumerate(files):
        record = 64 + i * 80
        ufw.number(record, 0 if i == 0 else 100, 2)
        ufw.number(record + 2, i, 2)
        ufw.number(record + 4, crc(data), 2)
        ufw.number(record + 8, 1024 if i == 0 else loader_at, 4)
        ufw.number(record + 12, len(data), 4)
        ufw.number(record + 16, round_up(len(data), 32), 4)
        ufw.text(record + 64, names[i])
        cipher(ufw.b, record, 80, 65535)
    ufw.number(4, total, 4)
    ufw.number(8, 2, 2)
    ufw.number(10, 4, 2)
    ufw.number(12, 512, 4)
    ufw.text(16, "AC791N")
    ufw.checksum(2, 64, 160)
    ufw.checksum(0, 2, 62)
    cipher(ufw.b, 0, 64, 65535)
    ufw.copy(1024, flash.b)
    ufw.copy(loader_at, loader)
    out = bytearray(total + 20)
    destination = 0
    for source in range(total):
        out[destination] = ufw.b[source]
        if source < 940 and (source + 1) % 47 == 0:
            index = source // 47
            destination += 1
            out[destination] = (ord(product[index]) + index + 1) & 0xFF if index < len(product) else 125
        destination += 1
    return bytes(out)


def create(application, resources, product):
    if not re.fullmatch(r"FM-1_8[0-9]{6}", product):
        raise SystemExit("Invalid FM1 update identity.")
    if not application or len(application) > FLASH_LIMIT - USER_SIZE - BT_SIZE - MIN_VM - AREA - APP_DATA - 383 - 64:
        raise SystemExit("Application does not fit the FM1 layout.")
    image_size = round_up(AREA + APP_DATA + len(application) + 383 + 64, 4096)
    user_at = FLASH_LIMIT - USER_SIZE
    vm_end = user_at - BT_SIZE
    if image_size > vm_end - MIN_VM:
        raise SystemExit("Insufficient FM1 settings space.")
    flash = build_flash(application, resources, image_size, vm_end)
    package = wrap(flash, open(os.path.join(resources, "ota.bin"), "rb").read(), product)
    return package, {"FLASH_BIN_SIZE": image_size, "VM_REAL_SIZE": vm_end - image_size, "user_bytes": USER_SIZE}


def main(argv):
    product, resources, args = None, None, []
    it = iter(argv)
    for a in it:
        if a == "--product":
            product = next(it)
        elif a == "--resources":
            resources = next(it)
        else:
            args.append(a)
    if len(args) != 2:
        print(__doc__)
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    resources = resources or os.path.join(root, "sdk", "tools", "pack")
    if product is None:
        version = open(os.path.join(root, "app", "version.h")).read()
        match = re.search(r'^#define FM1_UPDATE_PRODUCT "(FM-1_8[0-9]{6})"\s*$', version, re.M)
        if not match:
            raise SystemExit("Set a compatible FM1_UPDATE_PRODUCT in app/version.h.")
        product = match.group(1)
    elf = Elf(args[0])
    if struct.unpack_from("<I", elf.data, 24)[0] != ENTRY:
        raise SystemExit("Package entry must match the verified FM1 firmware entry.")
    sections = {s.name: s for s in elf.sections}
    image = bytearray()
    for name in (".text", ".data", ".ram0_data", ".cache_ram_data"):
        s = sections.get(name)
        if s is not None and s.size:
            image += elf.content(s)
    package, layout = create(bytes(image), resources, product)
    open(args[1], "wb").write(package)
    print(f"Offline package fits 1 MiB: image {layout['FLASH_BIN_SIZE']}, VM {layout['VM_REAL_SIZE']}, user {layout['user_bytes']} bytes")
    print(f"{args[1]}: {len(package)} bytes, product {product}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
