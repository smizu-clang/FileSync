#!/usr/bin/env python3
"""Safely copy changed files from one local directory to another."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
from pathlib import Path


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def sync(source: Path, destination: Path, apply: bool) -> int:
    source = source.expanduser().resolve()
    destination = destination.expanduser().resolve()

    if not source.is_dir():
        print(f"Error: source directory does not exist: {source}", file=sys.stderr)
        return 2
    if source == destination or source in destination.parents or destination in source.parents:
        print("Error: source and destination must be separate, non-nested directories.", file=sys.stderr)
        return 2

    actions: list[tuple[Path, Path]] = []
    for source_file in sorted(source.rglob("*")):
        if source_file.is_symlink():
            print(f"Skip symlink: {source_file.relative_to(source)}")
            continue
        if not source_file.is_file():
            continue
        target = destination / source_file.relative_to(source)
        if target.is_symlink() or (target.exists() and not target.is_file()):
            print(f"Error: destination path is not a regular file: {target}", file=sys.stderr)
            return 2
        if not target.exists() or not target.is_file() or digest(source_file) != digest(target):
            actions.append((source_file, target))

    if not actions:
        print("Already in sync; no files to copy.")
        return 0

    for source_file, target in actions:
        relative = source_file.relative_to(source)
        if apply:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target)
            print(f"Copied: {relative}")
        else:
            print(f"Would copy: {relative}")

    if not apply:
        print(f"Dry run: {len(actions)} file(s) would be copied. Add --apply to sync.")
    else:
        print(f"Sync complete: {len(actions)} file(s) copied.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Copy new or changed files from a source folder to a destination folder."
    )
    parser.add_argument("source", type=Path, help="folder to sync from")
    parser.add_argument("destination", type=Path, help="folder to sync to")
    parser.add_argument("--apply", action="store_true", help="perform the copies (default is a dry run)")
    args = parser.parse_args()
    return sync(args.source, args.destination, args.apply)


if __name__ == "__main__":
    raise SystemExit(main())
