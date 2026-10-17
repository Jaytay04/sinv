"""Summarize actual NIC VM captures; never manufacture a missing observation."""
import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def mac(value):
    text = "".join(c for c in str(value or "").upper() if c in "0123456789ABCDEF")
    return text if len(text) == 12 else None


def values(value):
    return value if isinstance(value, list) else [value]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replacement-run", required=True)
    parser.add_argument("--restored-run", required=True)
    parser.add_argument("--host-after", required=True, type=Path)
    parser.add_argument("--final-state", required=True, type=Path)
    args = parser.parse_args()
    worktree = Path(__file__).resolve().parents[3]
    repo = Path(__file__).resolve().parents[6]
    raw = repo / "spoof-handoff/reports/nic-vm-20261017"
    review = worktree / "spoof-handoff/reports/review-20261017"
    review.mkdir(parents=True, exist_ok=True)

    def relative(path):
        return path.resolve().relative_to(repo).as_posix()

    def evidence(path):
        return {"path": relative(path), "bytes": path.stat().st_size, "sha256": digest(path)}

    def capture(run_id):
        if Path(run_id).name != run_id or not run_id.startswith("uefi-windows-probe-"):
            raise ValueError("Invalid capture run ID")
        path = raw / run_id / "evidence/kernel-metadata.json"
        return read_json(path), evidence(path)

    original, original_ref = capture("uefi-windows-probe-20261017-222702-f3417d5f41")
    replacement, replacement_ref = capture(args.replacement_run)
    restored, restored_ref = capture(args.restored_run)
    if original.get("allTargetsMatch") is not True or restored.get("allTargetsMatch") is not True:
        raise ValueError("Original/restored all-reader verification is not complete")
    if replacement.get("requestedFilterState") != "Present":
        raise ValueError("Replacement capture lacks the bound-filter requirement")
    if len(replacement.get("evaluations", [])) != 4:
        raise ValueError("Replacement capture lacks four complete adapter evaluations")
    services = replacement.get("adapterServices", [])
    if len(services) != 1 or services[0].get("State") != "Running":
        raise ValueError("Replacement capture was not made with the filter Running")
    if restored.get("requestedFilterState") != "Absent" or restored.get("adapterServices"):
        raise ValueError("Restoration capture does not prove absence")
    if restored.get("codeIntegrity", {}).get("testSigning") is not False:
        raise ValueError("Restoration capture does not prove effective kernel test signing OFF")
    if any(restored.get("trustMembership", {"unknown": True}).values()):
        raise ValueError("Restoration capture retains guest certificate trust")
    if restored.get("bcd", {}).get("testSigning", {}).get("exists") is not False:
        raise ValueError("Guest BCD test-signing baseline was not restored")

    by_id = lambda data: {row["pnpDeviceId"]: row for row in data["evaluations"]}
    before, active, after = map(by_id, (original, replacement, restored))
    if set(before) != set(active) or set(before) != set(after) or len(before) != 4:
        raise ValueError("Adapter identity set changed between captures")
    proofs = []
    for pnp, row in before.items():
        checks = lambda entry: {check["source"]: check for check in entry["checks"]}
        b, a, r = map(checks, (row, active[pnp], after[pnp]))
        if set(b) != set(a) or set(b) != set(r) or len(b) != 6:
            raise ValueError("Read-path set is incomplete or inconsistent")
        for source in b:
            observed = [b[source]["actual"], a[source]["actual"], r[source]["actual"]]
            expected = [b[source]["expected"], a[source]["expected"], r[source]["expected"]]
            matched = [any(mac(v) == mac(want) and mac(v) is not None for v in values(got))
                       for got, want in zip(observed, expected)]
            proofs.append({"pnpDeviceId": pnp, "reader": source,
                           "original": observed[0], "expectedReplacement": expected[1],
                           "activeObserved": observed[1], "restored": observed[2],
                           "originalMatches": matched[0], "replacementMatches": matched[1],
                           "restoredMatches": matched[2], "roundTripPass": all(matched),
                           "sourceCaptures": [original_ref["path"], replacement_ref["path"], restored_ref["path"]]})
    coverage = []
    for source in sorted({p["reader"] for p in proofs}):
        rows = [p for p in proofs if p["reader"] == source]
        coverage.append({"reader": source, "adapters": len(rows),
                         "roundTripPass": sum(p["roundTripPass"] for p in rows),
                         "replacementMatches": sum(p["replacementMatches"] for p in rows),
                         "restoredMatches": sum(p["restoredMatches"] for p in rows)})
    host_before_path = raw / "host-originals-retry/readback.20261017T220316561Z.534ca308733c42848cbc25e34f05a9fd.json"
    host_before, host_after = read_json(host_before_path), read_json(args.host_after)
    if not host_before["allLockedOriginalsMatchReadOnlySources"] or not host_after["allLockedOriginalsMatchReadOnlySources"]:
        raise ValueError("Physical host originals do not match before and after")
    final_state = read_json(args.final_state)
    if final_state.get("virtualMachinesRunning") != 0:
        raise ValueError("VM RAM was not released")
    if not final_state.get("sourceVmxUnchanged") or not final_state.get("mainCheckoutIndexUnchanged"):
        raise ValueError("Protected source VM or main index changed")
    if final_state.get("hostSecureBootCachedFlag") != 1 or final_state.get("hostKernelTestSigning") is not False:
        raise ValueError("Host Secure Boot cache / kernel test-signing baseline did not match")
    if final_state.get("hostNicServicePresent") or any(final_state["hostTrustMembership"].values()):
        raise ValueError("An owned NIC service or trust membership remains on the host")

    evidence_files = [evidence(p) for p in sorted(raw.rglob("*"))
                      if p.is_file() and p.name != "SHA256SUMS.txt" and "temporary" not in p.parts]
    raw_manifest = raw / "SHA256SUMS.txt"
    raw_manifest.write_text("".join(f'{item["sha256"]}  {item["path"]}\n' for item in evidence_files), encoding="utf-8")
    source_paths = [worktree / "spoof-handoff/tools/device_filter/CMakeLists.txt",
                    worktree / "spoof-handoff/tools/device_filter/nic/nic_filter.c",
                    worktree / "spoof-handoff/tools/device_filter/nic/nic_policy.c",
                    worktree / "spoof-handoff/tools/device_filter/nic/test_vm_profile.c",
                    worktree / "spoof-handoff/tools/device_vm/invoke-mailbox.ps1",
                    worktree / "spoof-handoff/docs/NIC_VM_VALIDATION.md"]
    source_paths += sorted((worktree / "spoof-handoff/tools/nic_vm").glob("*.*"))
    source_paths += sorted((worktree / "spoof-handoff/tools/nic_vm_probe").glob("*.*"))
    artifact_root = repo / "spoof-handoff/artifacts/nic-vm-20261017"
    packages = sorted((artifact_root / "build").glob("*/package/*"))
    packages += [artifact_root / "native-build/Release/nic_native_read.exe",
                 artifact_root / "default-release-validation/cmake/Release/client_nic_filter.sys"]
    success = all(p["roundTripPass"] for p in proofs)
    regression_path = raw / "regression/20261017T220258Z.14bedf2a11ba/regression-summary.json"
    unit_path = raw / "tests/result.20261017T224218123Z.0be7ae5d67d441a694299d77d8c51803.json"
    regression, units = read_json(regression_path), read_json(unit_path)
    if regression["result"] != "PASS" or units["status"] != "PASS":
        raise ValueError("A required regression suite is not passing")
    source_evidence = [dict(evidence(p), repositoryPath=p.relative_to(worktree).as_posix()) for p in source_paths]
    report = {"schema": "nic-vm-validation.v1", "createdUtc": datetime.now(timezone.utc).isoformat(),
              "result": "VM_ALL_READER_PASS_HOST_PENDING" if success else "HOST_DEPLOYMENT_BLOCKED_BY_VM_READBACK",
              "physicalNicReplacementTestExecuted": False, "hostActivationRunbookReady": False,
              "scope": "Four approved guest-profile adapter MAC pairs; no physical-host activation.",
              "registrationFix": {"revision1RawNdisStatus": "0xC0010005", "revision2RawNdisStatus": "0x00000000",
                                  "newDefaultRevision": 2, "vmDiagnosticsDefault": False,
                                  "evidenceRevision1": "spoof-handoff/reports/nic-vm-20261017/uefi-windows-probe-20261017-222319-1fbeae2c5e/evidence/kernel-metadata.json",
                                  "evidenceRevision2": original_ref["path"]},
              "vm": read_json(raw / "guest-profile.json")["machine"],
              "coverage": coverage, "proofs": proofs,
              "captures": {"original": original_ref, "replacement": replacement_ref, "restored": restored_ref,
                           "hostBefore": evidence(host_before_path), "hostAfter": evidence(args.host_after)},
              "finalGuestPolicy": {"codeIntegrity": restored["codeIntegrity"], "bcdTestSigning": restored["bcd"]["testSigning"],
                                   "trustMembership": restored["trustMembership"], "secureBoot": restored["secureBoot"],
                                   "filterServiceAbsent": not restored["adapterServices"]},
              "finalState": final_state, "finalStateEvidence": evidence(args.final_state),
              "deploymentBoundary": "Close ordinary adapter API visibility and repeat the VM all-reader gate before physical-host deployment. OID/WMI success does not establish coverage of every reader.",
              "physicalHostProvenCategories": {"count": 8, "confirmed": 9,
                                               "priorEvidence": "Seven SMBIOS fields through RSMB/WMI and disk serial through storage IOCTL/WMI; NIC physical-host replacement remains unproven."},
              "softwareTests": {"existingRegressionMatrix": regression,
                                "existingRegressionEvidence": evidence(regression_path),
                                "newPowerShellAssertions": {"status": units["status"], "checks": len(units["tests"]), "liveProof": False, "evidence": evidence(unit_path)},
                                "newDefaultNdisRevision": {"compile": "PASS: unsigned driver built with revision 2", "defaultHostCTest": "2/2 PASS", "logs": "spoof-handoff/reports/nic-vm-20261017/default-release-validation/"}},
              "evidenceManifest": evidence(raw_manifest), "evidenceFiles": evidence_files,
              "sourceFiles": source_evidence,
              "packageAndReaderArtifacts": [evidence(p) for p in packages],
              "excluded": "VM disks, snapshots, credentials, signing private keys, compiler temporary files and generated build trees are not published."}
    text = json.dumps(report, indent=2) + "\n"
    out = review / "nic-vm-validation.json"
    out.write_text(text, encoding="utf-8")
    main_review = repo / "spoof-handoff/reports/review-20261017"
    (main_review / out.name).write_text(text, encoding="utf-8")
    hashes = report["sourceFiles"] + report["packageAndReaderArtifacts"] + [evidence(out), evidence(raw_manifest)]
    manifest_text = "".join(f'{row["sha256"]}  {row["path"]}\n' for row in hashes)
    for directory in (review, main_review):
        (directory / "nic-vm-SHA256SUMS.txt").write_text(manifest_text, encoding="utf-8")
    print(json.dumps({"result": report["result"], "report": str(out), "sha256": digest(out), "coverage": coverage}, indent=2))


if __name__ == "__main__":
    main()
