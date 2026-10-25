"""Boot-only QEMU fixtures on generated GPT/FAT16 disks; no host ESP access."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import time
import uuid
import zlib

SECTOR = 512


def pin(path):
    data = path.read_bytes()
    return {"Path": str(path), "Bytes": len(data), "Sha256": hashlib.sha256(data).hexdigest()}


def esp_image(files):
    """A small, disposable FAT16 ESP with LFN support and both GPT headers."""
    sectors, start, spc, roots, reserved, copies = 131072, 2048, 4, 512, 1, 2
    last = sectors - 34
    total = last - start + 1
    root_sectors, fat_sectors = roots * 32 // SECTOR, 1
    while True:
        clusters = (total - reserved - root_sectors - copies * fat_sectors) // spc
        needed = math.ceil((clusters + 2) * 2 / SECTOR)
        if needed == fat_sectors:
            break
        fat_sectors = needed
    assert 4085 <= clusters < 65525
    cluster_bytes = spc * SECTOR
    image = bytearray(sectors * SECTOR)
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xfff8, 0xffff)
    next_cluster = 2
    root = {"children": {}, "cluster": 0}
    for path, data in files.items():
        if data is None:
            node = root
            for part in path.strip("/").split("/"):
                node = node["children"].setdefault(part, {"children": {}})
            continue
        parts = path.strip("/").split("/")
        node = root
        for part in parts[:-1]:
            node = node["children"].setdefault(part, {"children": {}})
        node["children"][parts[-1]] = {"data": data}

    def allocate(node):
        nonlocal next_cluster
        count = 1 if "children" in node else max(1, math.ceil(len(node["data"]) / cluster_bytes))
        node["cluster"], node["count"] = next_cluster, count
        for i in range(count):
            struct.pack_into("<H", fat, (next_cluster + i) * 2,
                             0xffff if i + 1 == count else next_cluster + i + 1)
        next_cluster += count
        assert next_cluster < clusters + 2
        for child in node.get("children", {}).values():
            allocate(child)
    for node in root["children"].values():
        allocate(node)

    def entry(name, node):
        base, dot, ext = name.rpartition(".")
        if not dot:
            base, ext = name, ""
        clean = lambda text: "".join(c for c in text.upper() if c.isalnum())
        short_base = clean(base)
        long = len(short_base) > 8 or len(ext) > 3 or short_base != base.upper()
        if long:
            short_base = short_base[:6] + "~1"
        short_name = (short_base.ljust(8) + clean(ext)[:3].ljust(3)).encode("ascii")
        output = bytearray()
        if long:
            checksum = 0
            for value in short_name:
                checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 255
            units = list(struct.unpack("<" + "H" * len(name), name.encode("utf-16le"))) + [0]
            units += [0xffff] * ((-len(units)) % 13)
            chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
            positions = list(range(1, 11, 2)) + list(range(14, 26, 2)) + [28, 30]
            for index in range(len(chunks) - 1, -1, -1):
                record = bytearray(32)
                record[0], record[11], record[13] = index + 1 | (0x40 if index + 1 == len(chunks) else 0), 15, checksum
                for offset, unit in zip(positions, chunks[index]):
                    struct.pack_into("<H", record, offset, unit)
                output.extend(record)
        record = bytearray(32)
        record[:11] = short_name
        record[11] = 16 if "children" in node else 32
        date = ((2024 - 1980) << 9) | (1 << 5) | 1
        for offset in (16, 18, 24):
            struct.pack_into("<H", record, offset, date)
        struct.pack_into("<H", record, 26, node["cluster"])
        struct.pack_into("<I", record, 28, len(node.get("data", b"")))
        output.extend(record)
        return output

    data_start = start + reserved + copies * fat_sectors + root_sectors
    def cluster_offset(number):
        return (data_start + (number - 2) * spc) * SECTOR
    def emit(node, parent=0):
        if "data" in node:
            offset = cluster_offset(node["cluster"])
            image[offset:offset + len(node["data"])] = node["data"]
            return
        contents = bytearray()
        if node["cluster"]:
            for name, number in ((b".          ", node["cluster"]), (b"..         ", parent)):
                record = bytearray(32)
                record[:11], record[11] = name, 16
                struct.pack_into("<H", record, 26, number)
                contents.extend(record)
        for name, child in node["children"].items():
            contents.extend(entry(name, child))
            emit(child, node["cluster"])
        capacity = cluster_bytes if node["cluster"] else roots * 32
        assert len(contents) + 32 <= capacity
        offset = cluster_offset(node["cluster"]) if node["cluster"] else (start + reserved + copies * fat_sectors) * SECTOR
        image[offset:offset + len(contents)] = contents
    emit(root)
    for i in range(copies):
        offset = (start + reserved + i * fat_sectors) * SECTOR
        image[offset:offset + len(fat)] = fat
    bpb = bytearray(SECTOR)
    bpb[:11] = b"\xeb\x3c\x90MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", bpb, 11, SECTOR, spc, reserved, copies, roots,
                     total if total < 65536 else 0, 0xf8, fat_sectors, 63, 255, start, total if total >= 65536 else 0)
    bpb[36], bpb[38] = 0x80, 0x29
    struct.pack_into("<I", bpb, 39, 0x424f4f54)
    bpb[43:54], bpb[54:62], bpb[510:512] = b"BOOTCHAIN  ", b"FAT16   ", b"\x55\xaa"
    image[start * SECTOR:(start + 1) * SECTOR] = bpb
    image[446 + 4] = 0xee
    struct.pack_into("<II", image, 446 + 8, 1, sectors - 1)
    image[510:512] = b"\x55\xaa"
    entries = bytearray(128 * 128)
    entries[:16] = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").bytes_le
    entries[16:32] = uuid.uuid4().bytes_le
    struct.pack_into("<QQQ", entries, 32, start, last, 0)
    name = "BootChainTest".encode("utf-16le")
    entries[56:56 + len(name)] = name
    disk_guid = uuid.uuid4().bytes_le
    for current, alternate, table in ((1, sectors - 1, 2), (sectors - 1, 1, sectors - 33)):
        header = bytearray(struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x10000, 92, 0, 0,
                                      current, alternate, 34, last, disk_guid, table, 128, 128, zlib.crc32(entries)))
        struct.pack_into("<I", header, 16, zlib.crc32(header))
        image[current * SECTOR:current * SECTOR + 92] = header
        image[table * SECTOR:table * SECTOR + len(entries)] = entries
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--evidence-directory", type=Path, required=True)
    parser.add_argument("--qemu-directory", type=Path, default=Path(r"C:\Program Files\qemu"))
    args = parser.parse_args()
    evidence = args.evidence_directory.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    qemu = args.qemu_directory / "qemu-system-x86_64.exe"
    code = args.qemu_directory / "share" / "edk2-x86_64-code.fd"
    variables = args.qemu_directory / "share" / "edk2-i386-vars.fd"
    fixture = args.build_directory / "boot_chain_vm_fixture.efi"
    child = args.build_directory / "boot_chain_vm_child.efi"
    inputs = [pin(path) for path in (qemu, code, variables, fixture, child, Path(__file__))]
    results = []
    for case in ("existing-target", "missing-target", "missing-backup", "missing-parent"):
        case_dir = evidence / case
        case_dir.mkdir()
        files = {"EFI/BOOT/BOOTX64.EFI": fixture.read_bytes(), "case.txt": case.encode("ascii")}
        if case != "missing-backup":
            files["ib-bootmgfw.efi"] = child.read_bytes()
        if case not in ("missing-target", "missing-parent"):
            files["EFI/Microsoft/Boot/bootmgfw.efi"] = b"synthetic canonical placeholder\r\n"
        elif case == "missing-target":
            files["EFI/Microsoft/Boot/"] = None
            # Retained copy models a previous attempt interrupted after delete.
            files["ib-bootmgfw.tmp"] = child.read_bytes()
        disk = case_dir / "esp.img"
        disk.write_bytes(esp_image(files))
        before = pin(disk)
        vars_copy = case_dir / "vars.fd"
        vars_copy.write_bytes(variables.read_bytes())
        debug = case_dir / "debug.log"
        command = [str(qemu), "-machine", "q35,accel=tcg", "-m", "512", "-smp", "1",
                   "-display", "none", "-nodefaults", "-no-reboot", "-monitor", "none", "-serial", "none", "-nic", "none",
                   "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code}",
                   "-drive", f"if=pflash,unit=1,format=raw,file={vars_copy}",
                   "-drive", f"if=none,id=esp,format=raw,snapshot=on,file={disk}",
                   "-device", "virtio-blk-pci,drive=esp", "-debugcon", f"file:{debug}",
                   "-global", "isa-debugcon.iobase=0xe9", "-device", "isa-debug-exit,iobase=0xf4,iosize=4"]
        started = time.monotonic()
        timed_out = False
        try:
            run = subprocess.run(command, capture_output=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
            exit_code, stdout, stderr = run.returncode, run.stdout, run.stderr
        except subprocess.TimeoutExpired as exc:
            # subprocess.run kills and waits for this owned process on timeout.
            timed_out, exit_code = True, None
            stdout, stderr = exc.stdout or b"", exc.stderr or b""
        (case_dir / "stdout.txt").write_bytes(stdout)
        (case_dir / "stderr.txt").write_bytes(stderr)
        log = debug.read_text(errors="replace") if debug.exists() else ""
        markers = ["boot-only fixture: PASS"]
        if case == "missing-backup":
            markers += ["MISSING BACKUP FAILS CLOSED"]
        elif case == "missing-parent":
            markers += ["MISSING PARENT FAILS CLOSED"]
        else:
            markers += ["RESTORE READBACK PASS", "boot-only child: RETURN LOAD_ERROR", "FIRMWARE CHILD RETURN PASS"]
        unchanged = pin(disk) == before
        passed = not timed_out and exit_code == 33 and unchanged and all(marker in log for marker in markers) and "boot-only fixture: FAIL" not in log
        result = {"Case": case, "Passed": passed, "ExitCode": exit_code, "TimedOut": timed_out,
                  "Seconds": round(time.monotonic() - started, 2), "BaseDiskUnchanged": unchanged,
                  "BaseDiskPin": before, "VmStopped": True,
                  "Command": command, "DebugLog": str(debug)}
        results.append(result)
        (case_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    report = {"SchemaVersion": 1, "Status": "BOOT_ONLY_VM_PASS" if all(r["Passed"] for r in results) else "BOOT_ONLY_VM_FAIL",
              "Succeeded": all(r["Passed"] for r in results), "Inputs": inputs, "Cases": results,
              "Scope": "Stock OVMF, synthetic EFI child, real filesystem restoration; no Windows OS, Secure Boot trust, TPM spoofing, payload, or hypervisor validation.",
              "GuestMemoryMiB": 512, "Networking": False, "HostEspAccessed": False}
    (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"Status": report["Status"], "Cases": [{"Case": r["Case"], "Passed": r["Passed"]} for r in results], "Evidence": str(evidence / "result.json")}, indent=2))
    return 0 if report["Succeeded"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
