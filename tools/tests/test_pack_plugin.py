#!/usr/bin/env python3
"""Tests for pack_plugin.py — the .qgcplugin packer.

The tool's whole value is that a package it produces installs, so the cases here are the
installer's rejection reasons: each one must be caught at pack time, not left to fail on a
user's machine. The drift tests at the bottom are the other half — the tool duplicates the
installer's constants because it ships where there is no QGC source to read them from, and a
duplicate nobody checks is a duplicate that goes stale.
"""

from __future__ import annotations

import json
import struct
import zipfile
from pathlib import Path

import pytest
from pack_plugin import (
    ANDROID_NOTE_SECTION,
    MAX_PACKAGE_BYTES,
    PLATFORM_KEYS,
    PackError,
    check_binary_matches_key,
    collect_entries,
    find_binaries,
    load_manifest,
    main,
    read_elf_info,
)

REPO_ROOT = Path(__file__).resolve().parents[2]


def write_manifest(package_dir: Path, **overrides: object) -> dict[str, object]:
    manifest: dict[str, object] = {
        "id": "org.example.packed",
        "name": "Packed",
        "version": "1.0.0",
        "vendor": "Example",
        "tier": "sdk",
        "apiVersion": 2,
        "hostVersion": {"min": "5.0", "max": ""},
    }
    manifest.update(overrides)
    package_dir.mkdir(parents=True, exist_ok=True)
    (package_dir / "qgcplugin.json").write_text(json.dumps(manifest), encoding="utf-8")
    return manifest


def add_binary(package_dir: Path, name: str = "libPacked.dylib", key: str = "macos-universal"):
    binary = package_dir / "bin" / key / name
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_bytes(b"\xcf\xfa\xed\xfe not a real mach-o")
    return binary


def make_elf(machine: int = 183, android: bool = False, bits: int = 64, order: str = "<") -> bytes:
    """A minimal ELF shared object: a header, a section-name table and two section headers
    (null and the string table), plus a third named .note.android.ident when `android`."""
    names = b"\0.shstrtab\0" + (ANDROID_NOTE_SECTION.encode() + b"\0" if android else b"")
    note_name = names.index(b".note") if android else 0
    header_size = 64 if bits == 64 else 52
    shentsize = 64 if bits == 64 else 40
    count = 3 if android else 2
    shoff = header_size + len(names)
    ident = b"\x7fELF" + bytes([2 if bits == 64 else 1, 1 if order == "<" else 2, 1]) + bytes(9)
    if bits == 64:
        header = struct.pack(
            order + "HHIQQQIHHHHHH",
            3,
            machine,
            1,
            0,
            0,
            shoff,
            0,
            header_size,
            0,
            0,
            shentsize,
            count,
            1,
        )
    else:
        header = struct.pack(
            order + "HHIIIIIHHHHHH",
            3,
            machine,
            1,
            0,
            0,
            shoff,
            0,
            header_size,
            0,
            0,
            shentsize,
            count,
            1,
        )

    def section(name: int, offset: int, size: int) -> bytes:
        if bits == 64:
            return struct.pack(order + "IIQQQQIIQQ", name, 3, 0, 0, offset, size, 0, 0, 1, 0)
        return struct.pack(order + "IIIIIIIIII", name, 3, 0, 0, offset, size, 0, 0, 1, 0)

    sections = bytes(shentsize) + section(1, header_size, len(names))
    if android:
        sections += section(note_name, header_size, 0)
    assert len(ident + header) == header_size
    return ident + header + names + sections


def pack_elf(tmp_path: Path, data: bytes, key: str) -> int:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    loose = tmp_path / "libLoose.so"
    loose.write_bytes(data)
    return main(
        [
            str(package_dir),
            "-o",
            str(tmp_path / "out.qgcplugin"),
            "--binary",
            str(loose),
            "--platform",
            key,
        ]
    )


# --- Manifest validation ---------------------------------------------------------------


def test_missing_manifest_is_named(tmp_path: Path) -> None:
    (tmp_path / "pkg").mkdir()
    with pytest.raises(PackError, match=r"qgcplugin\.json"):
        load_manifest(tmp_path / "pkg")


@pytest.mark.parametrize("field", ["id", "name", "vendor", "version", "tier"])
def test_each_required_field_is_required(tmp_path: Path, field: str) -> None:
    package_dir = tmp_path / "pkg"
    manifest = write_manifest(package_dir)
    del manifest[field]
    (package_dir / "qgcplugin.json").write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(PackError, match=field):
        load_manifest(package_dir)


def test_malformed_json_reports_the_parse_error(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    package_dir.mkdir()
    (package_dir / "qgcplugin.json").write_text("{ not json", encoding="utf-8")
    with pytest.raises(PackError, match="malformed"):
        load_manifest(package_dir)


def test_non_numeric_version_rejected(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, version="v1.0.0")
    with pytest.raises(PackError, match="invalid 'version'"):
        load_manifest(package_dir)


def test_sdk_tier_requires_api_version(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    manifest = write_manifest(package_dir)
    del manifest["apiVersion"]
    (package_dir / "qgcplugin.json").write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(PackError, match="apiVersion"):
        load_manifest(package_dir)


def test_qml_tier_does_not_require_api_version(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    manifest = write_manifest(package_dir, tier="qml")
    del manifest["apiVersion"]
    (package_dir / "qgcplugin.json").write_text(json.dumps(manifest), encoding="utf-8")
    assert load_manifest(package_dir)["tier"] == "qml"


def test_internal_tier_cannot_be_packaged(tmp_path: Path) -> None:
    """The installer refuses it, so producing one at all would be producing a dud."""
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, tier="internal", hostBuildId="abc123")
    with pytest.raises(PackError, match="tier internal cannot be packaged"):
        load_manifest(package_dir)


# --- Layout rules ------------------------------------------------------------------------


def test_sdk_package_round_trips(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    (package_dir / "qml").mkdir()
    (package_dir / "qml" / "Panel.qml").write_text("import QtQuick\nItem {}\n", encoding="utf-8")
    output = tmp_path / "out.qgcplugin"

    assert main([str(package_dir), "-o", str(output)]) == 0

    with zipfile.ZipFile(output) as archive:
        names = sorted(archive.namelist())
    assert names == [
        "bin/macos-universal/libPacked.dylib",
        "qgcplugin.json",
        "qml/Panel.qml",
    ]


def test_manifest_lands_at_archive_root(tmp_path: Path) -> None:
    """PluginInstaller looks up 'qgcplugin.json' exactly, with no directory prefix — a
    package nested one level down is the classic `zip -r` mistake and installs as nothing."""
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    output = tmp_path / "out.qgcplugin"
    main([str(package_dir), "-o", str(output)])

    with zipfile.ZipFile(output) as archive:
        assert archive.namelist().count("qgcplugin.json") == 1
        assert "pkg/qgcplugin.json" not in archive.namelist()


def test_sdk_package_without_binary_rejected(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert "no plugin binary found" in capsys.readouterr().err


def test_two_binaries_rejected(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir, "libOne.dylib")
    add_binary(package_dir, "libTwo.dylib")
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert "expected exactly one" in capsys.readouterr().err


def test_qml_tier_with_binary_rejected(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, tier="qml")
    add_binary(package_dir)
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert "must not ship a binary" in capsys.readouterr().err


def test_qml_tier_packs_with_no_binary(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, tier="qml", qmlApiVersion=1)
    (package_dir / "qml").mkdir()
    (package_dir / "qml" / "Panel.qml").write_text("import QtQuick\nItem {}\n", encoding="utf-8")
    output = tmp_path / "out.qgcplugin"
    assert main([str(package_dir), "-o", str(output)]) == 0
    with zipfile.ZipFile(output) as archive:
        assert sorted(archive.namelist()) == ["qgcplugin.json", "qml/Panel.qml"]


@pytest.mark.parametrize("declared", ["replay", "telemetryLogging"])
def test_qml_tier_cannot_declare_binary_contributions(
    tmp_path: Path, capsys, declared: str
) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, tier="qml", contributes={declared: True})
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert declared in capsys.readouterr().err


def test_binary_outside_bin_is_not_a_candidate(tmp_path: Path) -> None:
    """find_binaries mirrors the loader: only bin/<subdir>/<file> counts. A .dylib under
    assets/ is inert cargo to the loader, so it must not satisfy the sdk-tier rule."""
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    (package_dir / "assets").mkdir()
    (package_dir / "assets" / "libStray.dylib").write_bytes(b"stray")
    entries = collect_entries(package_dir)
    assert find_binaries(entries) == []


# --- Installer safety rules ----------------------------------------------------------------


@pytest.mark.parametrize(
    "relative",
    [
        "bin/macos-universal/libQGCPluginAPI.dylib",
        "bin/macos-universal/QtCore.dylib",
        "assets/qtcore.framework/QtCore",
    ],
)
def test_bundled_runtime_rejected(tmp_path: Path, capsys, relative: str) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    target = package_dir / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(b"runtime")
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert "bundles a runtime library" in capsys.readouterr().err


def test_symlink_rejected(tmp_path: Path, capsys) -> None:
    """zipfile.write() follows symlinks, so packing one embeds its target's bytes — content
    from outside the package that the installer's entry-name checks cannot see."""
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    outside = tmp_path / "outside.txt"
    outside.write_text("secret", encoding="utf-8")
    (package_dir / "link.txt").symlink_to(outside)
    assert main([str(package_dir), "-o", str(tmp_path / "out.qgcplugin")]) == 1
    assert "symlink" in capsys.readouterr().err


def test_junk_files_excluded(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    (package_dir / ".DS_Store").write_bytes(b"junk")
    (package_dir / ".git").mkdir()
    (package_dir / ".git" / "config").write_text("[core]\n", encoding="utf-8")
    output = tmp_path / "out.qgcplugin"
    main([str(package_dir), "-o", str(output)])
    with zipfile.ZipFile(output) as archive:
        names = archive.namelist()
    assert not any(".DS_Store" in name or ".git" in name for name in names)


def test_executable_bit_preserved(tmp_path: Path) -> None:
    """A binary that arrives non-executable is an opaque dlopen failure, not a legible one."""
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    binary = add_binary(package_dir)
    binary.chmod(0o755)
    output = tmp_path / "out.qgcplugin"
    main([str(package_dir), "-o", str(output)])
    with zipfile.ZipFile(output) as archive:
        info = archive.getinfo("bin/macos-universal/libPacked.dylib")
    assert (info.external_attr >> 16) & 0o111


# --- CLI behaviour ---------------------------------------------------------------------------


def test_default_output_name_from_manifest(tmp_path: Path, monkeypatch) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    monkeypatch.chdir(tmp_path)
    assert main([str(package_dir)]) == 0
    assert (tmp_path / "org.example.packed-1.0.0.qgcplugin").is_file()


def test_existing_output_needs_force(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    output = tmp_path / "out.qgcplugin"
    output.write_bytes(b"old")
    assert main([str(package_dir), "-o", str(output)]) == 1
    assert "--force" in capsys.readouterr().err
    assert main([str(package_dir), "-o", str(output), "--force"]) == 0


def test_dry_run_writes_nothing(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    add_binary(package_dir)
    output = tmp_path / "out.qgcplugin"
    assert main([str(package_dir), "-o", str(output), "--dry-run"]) == 0
    assert not output.exists()
    assert "bin/macos-universal/libPacked.dylib" in capsys.readouterr().out


def test_binary_flag_stages_into_platform_dir(tmp_path: Path) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    loose = tmp_path / "libLoose.dylib"
    loose.write_bytes(b"binary")
    output = tmp_path / "out.qgcplugin"
    assert main([str(package_dir), "-o", str(output), "--binary", str(loose)]) == 0
    with zipfile.ZipFile(output) as archive:
        assert "bin/macos-universal/libLoose.dylib" in archive.namelist()


def test_binary_flag_rejected_for_qml_tier(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir, tier="qml")
    loose = tmp_path / "libLoose.dylib"
    loose.write_bytes(b"binary")
    assert (
        main([str(package_dir), "-o", str(tmp_path / "o.qgcplugin"), "--binary", str(loose)]) == 1
    )
    assert "qml-tier" in capsys.readouterr().err


def test_linux_arm64_key_packs_without_warning(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    loose = tmp_path / "libLoose.so"
    loose.write_bytes(make_elf())
    output = tmp_path / "out.qgcplugin"
    code = main(
        [str(package_dir), "-o", str(output), "--binary", str(loose), "--platform", "linux-arm64"]
    )
    assert code == 0
    assert "warning" not in capsys.readouterr().err
    with zipfile.ZipFile(output) as archive:
        assert "bin/linux-arm64/libLoose.so" in archive.namelist()


def test_unknown_platform_key_warns_but_packs(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    loose = tmp_path / "libLoose.dylib"
    loose.write_bytes(b"binary")
    output = tmp_path / "out.qgcplugin"
    code = main(
        [str(package_dir), "-o", str(output), "--binary", str(loose), "--platform", "macos-x86_64"]
    )
    assert code == 0
    assert "fallback" in capsys.readouterr().err


# --- Binary-versus-key check ---------------------------------------------------------------


def test_android_elf_packs_under_android_key_without_warning(tmp_path: Path, capsys) -> None:
    assert pack_elf(tmp_path, make_elf(android=True), "android-arm64") == 0
    assert capsys.readouterr().err == ""
    with zipfile.ZipFile(tmp_path / "out.qgcplugin") as archive:
        assert "bin/android-arm64/libLoose.so" in archive.namelist()


def test_glibc_elf_refused_under_android_key(tmp_path: Path, capsys) -> None:
    assert pack_elf(tmp_path, make_elf(android=False), "android-arm64") == 1
    err = capsys.readouterr().err
    assert "android-arm64" in err
    assert "glibc" in err


def test_android_elf_refused_under_linux_key(tmp_path: Path, capsys) -> None:
    assert pack_elf(tmp_path, make_elf(android=True), "linux-arm64") == 1
    err = capsys.readouterr().err
    assert "linux-arm64" in err
    assert "bionic" in err


@pytest.mark.parametrize("key", ["linux-arm64", "android-arm64"])
def test_x86_64_elf_refused_under_arm64_key(tmp_path: Path, capsys, key: str) -> None:
    assert pack_elf(tmp_path, make_elf(machine=62, android=key.startswith("android")), key) == 1
    assert "built for x86-64" in capsys.readouterr().err


def test_arm64_elf_refused_under_x64_key(tmp_path: Path, capsys) -> None:
    assert pack_elf(tmp_path, make_elf(machine=183), "linux-x64") == 1
    assert "built for AArch64" in capsys.readouterr().err


def test_package_dir_binary_is_checked_against_its_directory_key(tmp_path: Path, capsys) -> None:
    package_dir = tmp_path / "pkg"
    write_manifest(package_dir)
    binary = package_dir / "bin" / "android-arm64" / "libPacked.so"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(make_elf(android=False))
    assert main([str(package_dir), "-o", str(tmp_path / "o.qgcplugin")]) == 1
    assert "glibc" in capsys.readouterr().err


@pytest.mark.parametrize(
    ("bits", "order"), [(32, "<"), (64, ">"), (32, ">")], ids=["elf32", "big64", "big32"]
)
def test_elf_variants_are_read(bits: int, order: str, tmp_path: Path) -> None:
    path = tmp_path / "lib.so"
    path.write_bytes(make_elf(machine=183, android=True, bits=bits, order=order))
    info = read_elf_info(path)
    assert info is not None
    assert (info.machine, info.is_android) == (183, True)


def test_malformed_binaries_are_errors_not_crashes(tmp_path: Path) -> None:
    good = make_elf(android=True)
    cases = {
        "empty": b"",
        "not elf": b"binary, not an elf file",
        "magic only": b"\x7fELF",
        "truncated header": good[:30],
        "truncated sections": good[:-70],
        "bad class": b"\x7fELF\x07\x01\x01" + bytes(100),
    }
    for label, data in cases.items():
        path = tmp_path / "lib.so"
        path.write_bytes(data)
        with pytest.raises(PackError):
            check_binary_matches_key(path, "android-arm64")
        assert label


def test_huge_offsets_are_errors_not_crashes(tmp_path: Path) -> None:
    good = make_elf(android=True)
    path = tmp_path / "lib.so"
    # e_shoff is at byte 40 of an ELF64 header; the string table's sh_offset is at
    # shoff + shentsize + 24.
    shoff = struct.unpack_from("<Q", good, 40)[0]
    huge_shoff = bytearray(good)
    struct.pack_into("<Q", huge_shoff, 40, 0xFFFFFFFFFFFFFFFF)
    huge_str = bytearray(good)
    struct.pack_into("<Q", huge_str, shoff + 64 + 24, 0xFFFFFFFFFFFFFFFF)
    for data in (bytes(huge_shoff), bytes(huge_str)):
        path.write_bytes(data)
        assert read_elf_info(path) is None
        with pytest.raises(PackError, match="not a readable ELF"):
            check_binary_matches_key(path, "android-arm64")


def test_binary_without_section_headers_is_named_as_such(tmp_path: Path) -> None:
    data = bytearray(make_elf(android=True))
    struct.pack_into("<Q", data, 40, 0)  # e_shoff = 0
    path = tmp_path / "lib.so"
    path.write_bytes(bytes(data))
    with pytest.raises(PackError, match="no section headers"):
        check_binary_matches_key(path, "android-arm64")


def test_missing_binary_is_an_error_not_a_crash(tmp_path: Path) -> None:
    with pytest.raises(PackError, match="not a readable ELF"):
        check_binary_matches_key(tmp_path / "absent.so", "linux-x64")


def test_other_platform_keys_are_not_elf_checked(tmp_path: Path) -> None:
    path = tmp_path / "lib.dylib"
    path.write_bytes(b"\xcf\xfa\xed\xfe not an elf")
    check_binary_matches_key(path, "macos-universal")
    check_binary_matches_key(path, "windows-x64")


# --- Drift guards: the duplicated constants must match the C++ ------------------------------
#
# The tool ships inside the SDK zip, where there is no QGC source to read these from, so it
# carries its own copies. These tests are what keep the copies honest, and they run only in
# a checkout — the same shape verify_plugin_out_of_tree.py's Qt-component drift test uses.

INSTALLER_CC = REPO_ROOT / "src" / "PluginSystem" / "PluginInstaller.cc"
LOADER_CC = REPO_ROOT / "src" / "PluginSystem" / "QGCPluginLoader.cc"
CATALOG_CC = REPO_ROOT / "src" / "PluginSystem" / "PluginCatalog.cc"


@pytest.mark.skipif(not INSTALLER_CC.is_file(), reason="requires a QGC checkout")
def test_size_ceiling_matches_installer() -> None:
    text = INSTALLER_CC.read_text(encoding="utf-8")
    assert "kMaxPackageBytes = 512LL * 1024 * 1024" in text, (
        "PluginInstaller's extraction ceiling changed; update MAX_PACKAGE_BYTES"
    )
    assert MAX_PACKAGE_BYTES == 512 * 1024 * 1024


@pytest.mark.skipif(not LOADER_CC.is_file(), reason="requires a QGC checkout")
def test_platform_keys_match_loader() -> None:
    text = LOADER_CC.read_text(encoding="utf-8")
    for key in PLATFORM_KEYS:
        assert f'QStringLiteral("{key}")' in text, (
            f"platformBinarySubdir() no longer names {key}; update PLATFORM_KEYS"
        )


@pytest.mark.skipif(
    not CATALOG_CC.is_file(), reason="requires a QGC checkout with the plugin catalog"
)
def test_platform_keys_match_catalog_parser() -> None:
    text = CATALOG_CC.read_text(encoding="utf-8")
    for key in PLATFORM_KEYS:
        assert f'QStringLiteral("{key}")' in text, (
            f"isKnownPackageKey() no longer names {key}; update PLATFORM_KEYS or the parser"
        )
