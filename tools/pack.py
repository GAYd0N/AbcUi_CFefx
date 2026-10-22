#!/usr/bin/env python3
"""Pack first-party UI module packages.

Every directory under modules/ that carries a manifest.json becomes one zip
package at <out-dir>/<id>-<version>.zip:

    manifest.json            (archive root)
    entry/<abcui_id>.dll     (manifest "entry" path, STORED)
    hud/...                  (everything under the module's assets/, STORED)

Entries are stored uncompressed on purpose: the framework maps the archive
with CreateFileMapping and serves STORED entries as zero-copy views, so the
manifest, documents, stylesheets and the entry DLL never get copied.

Usage:
    pack.py --source-dir <repo> --build-dir <build> --config Release
            --out-dir <dir> [--module <dir> ...]

Exit code 0 only when every discovered module packed.
"""

import argparse
import json
import os
import sys
import zipfile

ABCUI_ABI_VERSION = 1
REQUIRED_FIELDS = (
    "id",
    "name",
    "description",
    "author",
    "version",
    "abi",
    "minAbcUi",
    "entry",
    "dependencies",
    "conflicts",
)
# Fixed timestamp so repeated packs are byte-identical.
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


def fail(message):
    print("pack: error: %s" % message, file=sys.stderr)
    return False


def load_manifest(module_dir):
    path = os.path.join(module_dir, "manifest.json")
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    for field in REQUIRED_FIELDS:
        if field not in manifest:
            raise ValueError("%s: missing field '%s'" % (path, field))
    if manifest["abi"] != ABCUI_ABI_VERSION:
        raise ValueError("%s: abi %r, expected %d" % (path, manifest["abi"],
                                                     ABCUI_ABI_VERSION))
    entry = manifest["entry"].replace("\\", "/")
    if entry.startswith("/") or ".." in entry.split("/"):
        raise ValueError("%s: entry must stay inside the package" % path)
    return manifest


def entry_dll_path(build_dir, module_dir, manifest, config):
    # modules/<dir>/<Config>/abcui_<suffix>.dll, suffix = the last id segment.
    suffix = manifest["id"].split(".")[-1]
    return os.path.join(build_dir, "modules", os.path.basename(module_dir),
                        config, "abcui_%s.dll" % suffix)


def asset_entries(module_dir):
    assets = os.path.join(module_dir, "assets")
    if not os.path.isdir(assets):
        return []
    found = []
    for root, _dirs, files in os.walk(assets):
        for name in files:
            absolute = os.path.join(root, name)
            relative = os.path.relpath(absolute, assets).replace("\\", "/")
            if relative.startswith(".."):
                raise ValueError("%s: asset escapes the package" % absolute)
            found.append((relative, absolute))
    return sorted(found)


def write_package(out_path, manifest, dll_path, assets):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_STORED) as archive:

        def add(name, data):
            info = zipfile.ZipInfo(name, ZIP_DATE)
            info.compress_type = zipfile.ZIP_STORED
            archive.writestr(info, data)

        add("manifest.json", json.dumps(manifest, indent=2,
                                        ensure_ascii=False) + "\n")
        entry = manifest["entry"].replace("\\", "/")
        with open(dll_path, "rb") as handle:
            add(entry, handle.read())
        for relative, absolute in assets:
            with open(absolute, "rb") as handle:
                add(relative, handle.read())


def pack_module(module_dir, build_dir, config, out_dir):
    manifest = load_manifest(module_dir)
    dll = entry_dll_path(build_dir, module_dir, manifest, config)
    if not os.path.isfile(dll):
        raise FileNotFoundError("missing entry DLL: %s (build the module "
                                "target for config %s first)" % (dll, config))
    assets = asset_entries(module_dir)
    out_path = os.path.join(out_dir, "%s-%s.zip" % (manifest["id"],
                                                    manifest["version"]))
    write_package(out_path, manifest, dll, assets)
    print("packed %s (%d asset(s))" % (out_path, len(assets)))
    return True


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--module", action="append", default=[],
                        help="module directory name (repeatable); default all")
    args = parser.parse_args(argv)

    modules_root = os.path.join(args.source_dir, "modules")
    if args.module:
        module_dirs = [os.path.join(modules_root, name)
                       for name in args.module]
    else:
        module_dirs = [os.path.join(modules_root, name)
                       for name in sorted(os.listdir(modules_root))
                       if os.path.isfile(os.path.join(modules_root, name,
                                                      "manifest.json"))]

    failures = 0
    packed = 0
    for module_dir in module_dirs:
        try:
            if pack_module(module_dir, args.build_dir, args.config,
                           args.out_dir):
                packed += 1
        except (OSError, ValueError, json.JSONDecodeError) as error:
            fail(str(error))
            failures += 1
    print("pack: %d package(s), %d failure(s)" % (packed, failures))
    return 0 if failures == 0 and packed > 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
