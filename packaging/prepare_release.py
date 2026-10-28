"""Collect release packages and generate checksums and the public update feed."""

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote


PACKAGE_SUFFIXES = (".zip", ".deb", ".rpm", ".tar.gz", ".dmg", ".AppImage", "-installer.exe")


def prepare_release(artifacts: Path, output: Path, repository: str, tag: str) -> dict:
    if not tag.startswith("v") or not tag[1:] or "/" in tag:
        raise ValueError("A version tag starting with v is required")
    packages = sorted(
        path for path in artifacts.rglob("Typist-*")
        if path.is_file() and path.name.endswith(PACKAGE_SUFFIXES)
    )
    if not packages:
        raise ValueError("No Typist release packages found")
    names = [path.name for path in packages]
    if len(names) != len(set(names)):
        raise ValueError("Release package filenames must be unique across platforms")
    if output.exists() and any(output.iterdir()):
        raise ValueError("The release output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)

    assets = []
    checksums = []
    for source in packages:
        destination = output / source.name
        shutil.copy2(source, destination)
        with destination.open("rb") as package:
            digest = hashlib.file_digest(package, "sha256").hexdigest()
        checksums.append(f"{digest}  {source.name}\n")
        assets.append({
            "name": source.name,
            "url": f"https://github.com/{repository}/releases/download/"
                   f"{quote(tag, safe='')}/{quote(source.name, safe='')}",
        })

    feed = {
        "version": tag[1:],
        "releasedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "assets": assets,
    }
    (output / "checksums.txt").write_text("".join(checksums), encoding="utf-8")
    (output / "latest.json").write_text(json.dumps(feed, indent=2) + "\n", encoding="utf-8")
    return feed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    prepare_release(args.artifacts, args.output, args.repository, args.tag)


if __name__ == "__main__":
    main()
