"""Seal actual seven-reader VM observations and their recovery evidence."""
import argparse
import csv
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def mac(value):
    text = "".join(c for c in str(value or "").upper() if c in "0123456789ABCDEF")
    return text if len(text) == 12 else None


def matches(actual, expected):
    values = actual if isinstance(actual, list) else [actual]
    return bool(values) and mac(expected) is not None and all(mac(v) == mac(expected) for v in values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original-run", "replacement-run", "unfiltered-run", "restored-run", "host-before", "host-after", "final-state", "recovery"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--gate-test-report", help="Pure tests of the final host gate; this is not a live host round trip")
    args = parser.parse_args()
    worktree = Path(__file__).resolve().parents[3]
    repo = Path(__file__).resolve().parents[6]
    raw = repo / "spoof-handoff/reports/nic-vm-20261017"
    new = repo / "spoof-handoff/reports/nic-reader-gap-20261017"
    review = worktree / "spoof-handoff/reports/review-20261017"
    review.mkdir(parents=True, exist_ok=True)

    def item(path):
        return {"path": path.resolve().relative_to(repo).as_posix(), "bytes": path.stat().st_size, "sha256": digest(path)}

    def capture(run):
        if Path(run).name != run or not run.startswith("uefi-windows-probe-"):
            raise ValueError("Invalid VM run identifier")
        path = raw / run / "evidence/kernel-metadata.json"
        data = read(path)
        if data.get("phase") != "Readback" or data.get("requiredReaderSet") != "CurrentSeven":
            raise ValueError("A fresh live seven-reader Readback phase is required")
        return data, item(path)

    before, before_ref = capture(args.original_run)
    active, active_ref = capture(args.replacement_run)
    unfiltered, unfiltered_ref = capture(args.unfiltered_run)
    after, after_ref = capture(args.restored_run)
    if not before.get("allTargetsMatch") or not after.get("allTargetsMatch"):
        raise ValueError("Original and restored VM gates must both pass")
    if len(active.get("adapterServices", [])) != 1 or active["adapterServices"][0]["State"] != "Running":
        raise ValueError("Replacement comparison requires an actually Running filter")
    if unfiltered.get("adapterServices") or unfiltered.get("requestedFilterState") != "AbsentConfigured" or not unfiltered.get("allTargetsMatch"):
        raise ValueError("Independent unfiltered seven-reader gate must pass")
    if after.get("adapterServices") or after.get("codeIntegrity", {}).get("testSigning") is not False:
        raise ValueError("Final VM removal and effective test-signing OFF are required")
    if any(after.get("trustMembership", {"unknown": True}).values()):
        raise ValueError("Final guest trust was not restored")

    with (repo / "spoof-handoff/reports/client-originals.tsv").open(encoding="utf-8-sig", newline="") as stream:
        locked = {mac(r["original"]): r["replacement"] for r in csv.DictReader(stream, delimiter="\t") if r["member"] == "FakeMacAddress"}
    rows = [{r["pnpDeviceId"]: r for r in c["evaluations"]} for c in (before, active, after, unfiltered)]
    if len(rows[0]) != 4 or not (set(rows[0]) == set(rows[1]) == set(rows[2]) == set(rows[3])):
        raise ValueError("The exact four guest identities must persist across the round trip")
    proofs = []
    required = {"OID_802_3_CURRENT_ADDRESS", "GetAdaptersAddresses", "GetAdaptersInfo", "GetIfTable2",
                "Win32_NetworkAdapter.MACAddress", "MSFT_NetAdapter.NetworkAddresses", "Get-NetAdapter.MacAddress"}
    for pnp, row in rows[0].items():
        original = row["expected"]
        replacement = locked[mac(original)]
        checks = [{c["source"]: c for c in by_id[pnp]["checks"]} for by_id in rows]
        if not (set(checks[0]) == set(checks[1]) == set(checks[2]) == set(checks[3]) == required):
            raise ValueError("All seven independent readers are mandatory")
        if not all(c["matchesExpected"] for by_id in rows for c in by_id[pnp]["identityChecks"]):
            raise ValueError("An adapter identity check failed")
        for source in checks[0]:
            values = [c[source]["actual"] for c in checks]
            expected = [original, replacement, original, replacement]
            good = [matches(v, e) for v, e in zip(values, expected)]
            proofs.append({"pnpDeviceId": pnp, "reader": source, "original": values[0],
                           "expectedReplacement": replacement, "activeObserved": values[1], "restored": values[2],
                           "unfilteredObserved": values[3], "unfilteredMatches": good[3],
                           "originalMatches": good[0], "replacementMatches": good[1], "restoredMatches": good[2],
                           "roundTripPass": all(good)})
    coverage = [{"reader": source, "adapters": 4,
                 "replacementMatches": sum(p["replacementMatches"] for p in proofs if p["reader"] == source),
                 "restoredMatches": sum(p["restoredMatches"] for p in proofs if p["reader"] == source)}
                for source in sorted({p["reader"] for p in proofs})]
    host_before, host_after = (Path(args.host_before), Path(args.host_after))
    if not all(read(p)["allLockedOriginalsMatchReadOnlySources"] for p in (host_before, host_after)):
        raise ValueError("Fresh host originals did not match")
    latest_host = read(host_after)
    host_original_rows = latest_host["lockedOriginalEvaluations"]
    if len(host_original_rows) != 4 or not all(r.get("allRequiredExpectedSourcesMatch") for r in host_original_rows):
        raise ValueError("The host original comparison must cover all four locked adapters")
    if latest_host.get("expectedMode") != "Original" or latest_host.get("readOnly") is not True:
        raise ValueError("A read-only host Original comparison is required")
    final = read(Path(args.final_state))
    if final["virtualMachinesRunning"] != 0 or not final["mainCheckoutIndexUnchanged"] or not final["sourceVmxUnchanged"]:
        raise ValueError("VM resources or protected main/source state were not restored")
    if final["hostNicServicePresent"] or final["hostKernelTestSigning"] or any(final["hostTrustMembership"].values()):
        raise ValueError("Unexpected host NIC or policy state")

    start = datetime.fromisoformat(read(new / "before-state.json")["utc"])
    evidence = []
    for root in (new, raw):
        for path in sorted(root.rglob("*")):
            if not path.is_file() or "temporary" in path.parts or path.name.startswith("SHA256SUMS"):
                continue
            if root == raw and path.stat().st_mtime < start.timestamp():
                continue
            evidence.append(item(path))
    source = sorted((worktree / "spoof-handoff/tools/nic_vm").glob("*.ps1"))
    source += sorted((worktree / "spoof-handoff/tools/nic_vm").glob("*.py"))
    source += sorted((worktree / "spoof-handoff/tools/nic_vm_probe").glob("*.*"))
    source += [worktree / "spoof-handoff/docs/NIC_VM_VALIDATION.md"]
    source += [worktree / "spoof-handoff/commands/nic-host-test.ps1",
               worktree / "spoof-handoff/commands/nic-host-rollback.ps1"]
    success = all(p["roundTripPass"] for p in proofs)
    recovery_path = Path(args.recovery)
    if read(recovery_path).get("status") != "RECOVERED_AND_POWERED_OFF":
        raise ValueError("A completed disposable VM recovery is required")
    gate = {"schema": "nic-vm-host-gate.v1", "status": "VM_SEVEN_READER_ROUND_TRIP_PASS", "adapters": 4,
            "readers": sorted(required), "lockedTsvSha256": digest(repo / "spoof-handoff/reports/client-originals.tsv"),
            "nativeExeSha256": digest(repo / "spoof-handoff/artifacts/nic-reader-gap-20261017/native-build-vs18/Release/nic_native_read.exe"),
            "captures": {"original": before_ref, "filteredActive": active_ref, "unfilteredActive": unfiltered_ref, "restored": after_ref},
            "proofs": proofs, "vmRecoveredAndPoweredOff": True, "recoveryEvidence": item(recovery_path)}
    if not success:
        raise ValueError("A failing VM comparison cannot authorize a host test")
    gate_path = new / "vm-seven-reader-gate.json"
    gate_text = json.dumps(gate, indent=2) + "\n"
    if gate_path.exists() and read(gate_path) != gate:
        raise ValueError("Refusing to overwrite an existing different VM gate")
    if not gate_path.exists():
        gate_path.write_text(gate_text, encoding="utf-8")
    evidence = list({row["path"]: row for row in evidence + [item(gate_path)]}.values())
    counts_path = new / "independent-validation/suite-counts.json"
    counts = read(counts_path)
    if not counts.get("allSuitesPass"):
        raise ValueError("Independent software test suites did not pass")
    launch_errors = sorted((new / "host-launches").glob("*/start-error.json"))
    host_attempts = [{"evidence": item(p), "result": read(p),
                      "elevatedProcessStarted": (p.parent / "start.json").exists(),
                      "hostResultExists": (p.parent / "result.json").exists()}
                     for p in launch_errors]
    if any(r["elevatedProcessStarted"] or r["hostResultExists"] for r in host_attempts):
        raise ValueError("A host transaction needs separate outcome analysis before publishing this host-pending report")
    gate_test = None
    if args.gate_test_report:
        test_path = Path(args.gate_test_report)
        test_result = read(test_path)
        if test_result.get("status") != "PASS":
            raise ValueError("The revised host raw-input gate tests did not pass")
        if test_result.get("source", {}).get("sha256") != digest(worktree / "spoof-handoff/tools/nic_vm/host-address-roundtrip.ps1"):
            raise ValueError("The raw-input gate tests do not match the final host source")
        if test_result.get("testSource", {}).get("sha256") != digest(worktree / "spoof-handoff/tools/nic_vm/host-gate-tests.ps1"):
            raise ValueError("The raw-input gate report does not match the final test source")
        if test_result.get("summary", {}).get("rawInputComparisons") != 112 or test_result["summary"].get("failed") != 0:
            raise ValueError("All 112 raw-input gate comparisons must pass")
        if test_result["summary"].get("negativeCases") != 3 or test_result["summary"].get("deviceOrHostActions") != 0:
            raise ValueError("The gate test must reject three negative cases without host/device actions")
        gate_test = {"evidence": item(test_path), "result": test_result}
    report = {"schema": "nic-seven-reader-validation.v1", "createdUtc": datetime.now(timezone.utc).isoformat(),
              "result": "VM_SEVEN_READER_PASS_HOST_PENDING" if success else "HOST_DEPLOYMENT_BLOCKED_BY_VM_GATE",
              "physicalHostReplacementExecuted": False, "physicalHostProvenCategories": "8/9; NIC remains unproven on the physical host",
              "physicalHostOriginals": [{"pnpDeviceId": r["pnpDeviceId"], "original": r["lockedOriginal"],
                                         "lockedReplacement": r["lockedReplacement"],
                                         "requiredReaders": r["requiredExpectedSourceCount"],
                                         "originalReadersMatch": r["allRequiredExpectedSourcesMatch"],
                                         "observations": r["observations"],
                                         "dot11CurrentAddress": r.get("dot11CurrentAddressCheck")}
                                        for r in host_original_rows],
              "coverage": coverage, "proofs": proofs,
              "captures": {"original": before_ref, "active": active_ref, "unfilteredActive": unfiltered_ref, "restored": after_ref,
                           "hostBefore": item(host_before), "hostAfter": item(host_after)},
              "finalState": final, "finalStateEvidence": item(Path(args.final_state)),
              "softwareTests": {"independentPureSuites": counts, "independentSuiteEvidence": item(counts_path),
                                "revisedRawInputGate": gate_test,
                                "ctestPasses": {"deviceFilter": 2, "identifier": 8, "smbios": 2,
                                                "uefiLoaderPure": 27, "nativeDeviceIo": 1},
                                "ctestEvidenceDirectory": "spoof-handoff/reports/nic-reader-gap-20261017/regression",
                                "ctestExclusion": "The unbuilt optional nested_e2e_service_test was not executed; initial Not Run and the 27-test pure rerun are both preserved.",
                                "olderFrozenHostTestScope": "The 15 AST/mock checks in independentPureSuites tested their recorded source hash. The final raw-input gate revision is validated separately; neither is live host proof."},
              "hostLaunchAttempts": host_attempts,
              "evidenceFiles": evidence, "sourceFiles": [dict(item(p), repositoryPath=p.relative_to(worktree).as_posix()) for p in source],
              "readerArtifact": item(repo / "spoof-handoff/artifacts/nic-reader-gap-20261017/native-build-vs18/Release/nic_native_read.exe"),
              "scope": "Current adapter MACs only. Permanent addresses remain informational. VM adapters are two e1000e and two vmxnet3, not the physical host miniports.",
              "missing": "Physical-host all-reader round trip is pending. The administrator launch was canceled before the host script started; run the reviewed command from an elevated PowerShell session.",
              "excluded": "No private keys, credentials, VM disks, snapshots or generated build trees are published."}
    output = review / "nic-reader-gap-validation.json"
    text = json.dumps(report, indent=2) + "\n"
    output.write_text(text, encoding="utf-8")
    main_review = repo / "spoof-handoff/reports/review-20261017"
    (main_review / output.name).write_text(text, encoding="utf-8")
    hashes = evidence + report["sourceFiles"] + [report["readerArtifact"], item(output)]
    manifest = "".join(f'{r["sha256"]}  {r["path"]}\n' for r in hashes)
    for root in (review, main_review):
        (root / "nic-reader-gap-SHA256SUMS.txt").write_text(manifest, encoding="utf-8")
    print(json.dumps({"result": report["result"], "coverage": coverage, "report": str(output), "sha256": digest(output)}, indent=2))


if __name__ == "__main__":
    main()
