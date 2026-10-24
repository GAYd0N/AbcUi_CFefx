#!/usr/bin/env python3
"""Pack first-party UI module packages.

Every release directory under modules/ carrying a manifest.json becomes one zip
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
import re
import sys
import zipfile

ABCUI_ABI_VERSION = 2
REQUIRED_FIELDS = (
    "id",
    "name",
    "description",
    "author",
    "version",
    "abi",
    "minAbcUi",
)
# Fixed timestamp so repeated packs are byte-identical.
ZIP_DATE = (1980, 1, 1, 0, 0, 0)
# The SDK example is opt-in, never a release UI feature.
EXAMPLE_MODULES = {"demo"}


def fail(message):
    print("pack: error: %s" % message, file=sys.stderr)
    return False


def load_manifest(module_dir):
    path = os.path.join(module_dir, "manifest.json")
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise ValueError("%s: manifest must be an object" % path)
    for field in REQUIRED_FIELDS:
        if field not in manifest:
            raise ValueError("%s: missing field '%s'" % (path, field))
        if field != "abi":
            value = manifest[field]
            if not isinstance(value, str) or "\0" in value:
                raise ValueError("%s: '%s' must be a string without nulls" %
                                 (path, field))
    if type(manifest["abi"]) is not int or manifest["abi"] != ABCUI_ABI_VERSION:
        raise ValueError("%s: abi %r, expected %d" % (path, manifest["abi"],
                                                     ABCUI_ABI_VERSION))
    id_pattern = r"[a-z0-9]+(?:\.[a-z0-9]+)*"
    if re.fullmatch(id_pattern, manifest["id"]) is None:
        raise ValueError("%s: invalid module id" % path)
    for field in ("version", "minAbcUi"):
        if re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", manifest[field]) is None:
            raise ValueError("%s: '%s' must be a dotted decimal version" %
                             (path, field))
    for field in ("dependencies", "conflicts"):
        values = manifest.setdefault(field, [])
        if not isinstance(values, list) or any(
                not isinstance(value, str) or
                re.fullmatch(id_pattern, value) is None for value in values):
            raise ValueError("%s: '%s' must be an array of module ids" %
                             (path, field))
    kind = manifest.get("type", "module")
    if kind not in ("module", "resources"):
        raise ValueError("invalid package type")
    if kind == "resources":
        if "entry" in manifest:
            raise ValueError("resource package must not declare an entry")
    else:
        entry = manifest.get("entry")
        if not isinstance(entry, str) or not entry or "\0" in entry:
            raise ValueError("module package needs a nonempty entry")
        entry = entry.replace("\\", "/")
        if ":" in entry or any(part in ("", ".", "..") for part in entry.split("/")):
            raise ValueError("entry must be a relative package path")
        manifest["entry"] = entry
    return manifest


def entry_dll_path(build_dir, module_dir, manifest, config):
    # modules/<dir>/<Config>/abcui_<suffix>.dll, suffix = the last id segment.
    suffix = manifest["id"].split(".")[-1]
    return os.path.join(build_dir, "modules", os.path.basename(module_dir),
                        config, "abcui_%s.dll" % suffix)


def asset_entries(module_dir, config="Release"):
    assets = os.path.join(module_dir, "assets")
    if not os.path.isdir(assets):
        return []
    found = []
    for root, _dirs, files in os.walk(assets):
        for name in files:
            absolute = os.path.join(root, name)
            relative = os.path.relpath(absolute, assets).replace("\\", "/")
            if config != "Debug" and relative.startswith("debug/"):
                continue
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
        if dll_path is not None:
            entry = manifest["entry"]
            with open(dll_path, "rb") as handle:
                add(entry, handle.read())
        for relative, absolute in assets:
            with open(absolute, "rb") as handle:
                add(relative, handle.read())


def pack_module(module_dir, build_dir, config, out_dir):
    manifest = load_manifest(module_dir)
    dll = None if manifest.get("type") == "resources" else entry_dll_path(build_dir, module_dir, manifest, config)
    if dll is not None and not os.path.isfile(dll):
        raise FileNotFoundError("missing entry DLL: %s (build the module "
                                "target for config %s first)" % (dll, config))
    assets = asset_entries(module_dir, config)
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
                        help="module directory name (repeatable); default release modules")
    args = parser.parse_args(argv)

    modules_root = os.path.join(args.source_dir, "modules")
    if args.module:
        module_dirs = [os.path.join(modules_root, name)
                       for name in args.module]
    else:
        module_dirs = [os.path.join(modules_root, name)
                       for name in sorted(os.listdir(modules_root))
                       if name not in EXAMPLE_MODULES and
                       os.path.isfile(os.path.join(modules_root, name,
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
