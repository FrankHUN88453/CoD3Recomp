#!/usr/bin/env python3
"""Standalone Xbox 360 XDVDFS ISO reader and extractor (mirrors CoD3Host/xdvdfs.cpp).

Allows listing or extracting default.xex, level DLLs, or full disc contents
from an Xbox 360 ISO (XGD1, XGD2, XGD3, or stripped) during Phase 1 before
CoD3Host is compiled.
"""
import argparse
import os
import pathlib
import struct
import sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
FORMATS = [
    (0x00000000, "stripped or rebuilt image"),
    (0x02080000, "XGD3"),
    (0x0FD90000, "XGD2"),
    (0x18300000, "XGD1"),
]
VOLUME_DESCRIPTOR_SECTOR = 32
SECTOR_SIZE = 2048
ATTR_DIRECTORY = 0x10


class XDvdFsImage:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self.file = open(path, "rb")
        self.file.seek(0, os.SEEK_END)
        self.file_size = self.file.tell()
        self.base_offset = 0
        self.format_name = ""
        self.root_sector = 0
        self.root_size = 0
        self._detect()

    def close(self):
        if self.file:
            self.file.close()
            self.file = None

    def read_at(self, offset: int, size: int) -> bytes:
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise RuntimeError(f"Short read at 0x{offset:X} ({len(data)} < {size})")
        return data

    def _detect(self):
        for base, name in FORMATS:
            vd_offset = base + VOLUME_DESCRIPTOR_SECTOR * SECTOR_SIZE
            if vd_offset + SECTOR_SIZE > self.file_size:
                continue
            sector = self.read_at(vd_offset, SECTOR_SIZE)
            if sector[:20] == MAGIC and sector[0x7EC:0x7EC + 20] == MAGIC:
                self.base_offset = base
                self.format_name = name
                self.root_sector, self.root_size = struct.unpack_from("<II", sector, 20)
                return
        raise RuntimeError(f"{self.path} is not a recognized Xbox/Xbox 360 XDVDFS ISO image")

    def list_files(self):
        entries = []
        if self.root_sector == 0 or self.root_size == 0:
            return entries
        self._walk_dir(self.root_sector, self.root_size, "", entries, 0)
        entries.sort(key=lambda e: e["path"].lower())
        return entries

    def _walk_dir(self, sector: int, size: int, prefix: str, out: list, depth: int):
        if depth > 64 or size == 0:
            return
        table = self.read_at(self.base_offset + sector * SECTOR_SIZE, size)
        stack = [0]
        visited = set()
        while stack:
            offset = stack.pop()
            if offset in visited or offset + 14 > len(table):
                continue
            visited.add(offset)
            left, right, start_sector, file_size, attrs, name_len = struct.unpack_from("<HHIIBB", table, offset)
            if left == 0xFFFF and right == 0xFFFF and start_sector == 0xFFFFFFFF:
                continue
            if offset + 14 + name_len > len(table):
                continue
            name = table[offset + 14:offset + 14 + name_len].decode("latin1", errors="replace")
            full_path = f"{prefix}/{name}" if prefix else name
            if right != 0 and right != 0xFFFF:
                stack.append(right * 4)
            if left != 0 and left != 0xFFFF:
                stack.append(left * 4)
            if attrs & ATTR_DIRECTORY:
                if file_size > 0:
                    self._walk_dir(start_sector, file_size, full_path, out, depth + 1)
            else:
                out.append({
                    "path": full_path,
                    "sector": start_sector,
                    "size": file_size,
                    "offset": self.base_offset + start_sector * SECTOR_SIZE,
                })

    def extract_file(self, entry: dict, dest: pathlib.Path):
        dest.parent.mkdir(parents=True, exist_ok=True)
        remaining = entry["size"]
        self.file.seek(entry["offset"])
        with open(dest, "wb") as out:
            while remaining > 0:
                chunk = self.file.read(min(remaining, 4 * 1024 * 1024))
                if not chunk:
                    raise RuntimeError(f"Unexpected EOF extracting {entry['path']}")
                out.write(chunk)
                remaining -= len(chunk)


def main():
    parser = argparse.ArgumentParser(description="Xbox 360 XDVDFS ISO utility")
    parser.add_argument("iso", type=pathlib.Path, help="Path to Xbox 360 .iso")
    parser.add_argument("--list", action="store_true", help="List all files in ISO")
    parser.add_argument("--extract", nargs=2, metavar=("INSIDE_PATH", "DEST_FILE"), help="Extract one file")
    parser.add_argument("--extract-all", type=pathlib.Path, metavar="DEST_DIR", help="Extract entire ISO")
    args = parser.parse_args()

    img = XDvdFsImage(args.iso)
    try:
        entries = img.list_files()
        if args.list or (not args.extract and not args.extract_all):
            total = sum(e["size"] for e in entries)
            has_xex = any(e["path"].lower() == "default.xex" for e in entries)
            print(f"{args.iso}")
            print(f"  filesystem  {img.format_name}, starting at offset 0x{img.base_offset:X}")
            print(f"  contents    {len(entries)} files, {total / (1024**3):.2f} GB")
            print(f"  default.xex {'present' if has_xex else 'MISSING'}\n")
            for e in entries:
                print(f"  {e['size']:12d}  {e['path']}")
            return 0 if has_xex else 1

        if args.extract:
            inside, dest = args.extract[0].replace("\\", "/").lower(), pathlib.Path(args.extract[1])
            for e in entries:
                if e["path"].lower() == inside:
                    img.extract_file(e, dest)
                    print(f"{e['path']} -> {dest} ({e['size']} bytes)")
                    return 0
            print(f"Error: {args.extract[0]} not found in {args.iso}", file=sys.stderr)
            return 1

        if args.extract_all:
            for i, e in enumerate(entries, 1):
                dest = args.extract_all / e["path"]
                img.extract_file(e, dest)
                print(f"\r  [{i}/{len(entries)}] {e['path']:<48}", end="", flush=True)
            print("\nDone.")
            return 0
    finally:
        img.close()


if __name__ == "__main__":
    sys.exit(main())
