#!/usr/bin/env python3
"""
ax-rtm-verify.py — Requirements Traceability Matrix Verification

DVEC: v1.3
SRS-004 v0.3 — Oracle Boundary Gateway, Inference Containment

Verifies that all 48 SHALL requirements from SRS-004 have explicit
code anchors in the axioma-oracle source tree.

Exit codes: 0 = CONFORMANT, 1 = NON-CONFORMANT
"""

import argparse
import re
import sys
from pathlib import Path

# =============================================================================
# SRS-004 v0.3 — Oracle Boundary Gateway Requirements (48 SHALL)
# =============================================================================

SRS_004_REQUIREMENTS = {
    # Containment (SHALL-001 to SHALL-004)
    "SRS-004-SHALL-001": "Containment principle — oracle outputs wrapped in AX:OBS:v1",
    "SRS-004-SHALL-002": "Deterministic downstream — canonical form enables replay",
    "SRS-004-SHALL-003": "Mandatory admission — all oracle outputs pass through admission",
    "SRS-004-SHALL-004": "No direct use — raw oracle output never used directly",
    
    # Admission (SHALL-005, SHALL-038)
    "SRS-004-SHALL-005": "Admission ordering — ledger_seq monotonically increasing",
    "SRS-004-SHALL-038": "Observation ordering guard — reject regression/duplicate",
    
    # Canonicalisation (SHALL-006, SHALL-036, SHALL-042-047)
    "SRS-004-SHALL-006": "Canonical form — RFC 8785 (JCS) serialisation",
    "SRS-004-SHALL-036": "Parameter canonicalisation — lexicographic key order",
    "SRS-004-SHALL-042": "Encoding canonicality — UTF-8 + NFC validation",
    "SRS-004-SHALL-043": "Output normalisation — line ending CRLF/CR → LF",
    "SRS-004-SHALL-044": "Control character rejection — forbidden except LF",
    "SRS-004-SHALL-045": "String escaping canonicality — minimal escaping",
    "SRS-004-SHALL-046": "Observation hash — SHA-256 over JCS(record with obs_hash='')",
    "SRS-004-SHALL-047": "Schema versioning — AX:OBS:v1 version string",
    
    # Observation Completeness (SHALL-035, SHALL-048)
    "SRS-004-SHALL-035": "Observation completeness — all required fields present",
    "SRS-004-SHALL-048": "Maximum observation size — 64KB limit enforced",
    
    # Oracle Identity (SHALL-007, SHALL-008)
    "SRS-004-SHALL-007": "Oracle identity — unique oracle_id per provider",
    "SRS-004-SHALL-008": "Model identity — model_id captures version",
    
    # Input Reproducibility (SHALL-009, SHALL-037)
    "SRS-004-SHALL-009": "Prompt determinism — input_hash anchors prompt",
    "SRS-004-SHALL-037": "Input schema enforcement — input hash computation",
    
    # Non-Determinism Bounding (SHALL-010 to SHALL-014)
    "SRS-004-SHALL-010": "Output bounding — maximum output size enforced",
    "SRS-004-SHALL-011": "Schema enforcement — validation on admission",
    "SRS-004-SHALL-012": "Sampling recording — temperature/top_p/seed captured",
    "SRS-004-SHALL-013": "Completion state — COMPLETE/TRUNCATED/ERROR",
    "SRS-004-SHALL-014": "Failure type — NULL/TIMEOUT/INVALID_OUTPUT/TRANSPORT_ERROR",
    
    # Oracle Isolation (SHALL-015)
    "SRS-004-SHALL-015": "Oracle isolation — no state leakage between calls",
    
    # Policy Gating (SHALL-016 to SHALL-019)
    "SRS-004-SHALL-016": "Policy gating — observation triggers L4 evaluation",
    "SRS-004-SHALL-017": "Policy binding — ledger_seq links to AX:POLICY:v1",
    "SRS-004-SHALL-018": "Policy rejection — violation prevents downstream use",
    "SRS-004-SHALL-019": "Policy audit — rejection recorded in audit ledger",
    
    # Execution Pipeline (SHALL-020)
    "SRS-004-SHALL-020": "Execution pipeline — admit → evaluate → commit",
    
    # Replay Model (SHALL-021, SHALL-022)
    "SRS-004-SHALL-021": "Replay identity — identical input → identical obs_hash",
    "SRS-004-SHALL-022": "Replay determinism — canonical form enables replay",
    
    # Side-Effect Prohibition (SHALL-023)
    "SRS-004-SHALL-023": "Side-effect prohibition — admission is pure",
    
    # Failure Semantics (SHALL-024 to SHALL-026)
    "SRS-004-SHALL-024": "Failure semantics — errors produce valid records",
    "SRS-004-SHALL-025": "Failure propagation — faults propagate to L4/L5",
    "SRS-004-SHALL-026": "Failure totality — all failure modes handled",
    
    # Purity Restoration (SHALL-027)
    "SRS-004-SHALL-027": "Purity restoration — non-determinism bounded at admission",
    
    # Policy DSL (SHALL-028 to SHALL-032)
    "SRS-004-SHALL-028": "Policy DSL — threshold comparisons in Q16.16",
    "SRS-004-SHALL-029": "Fixed-point conversion — Q16_16_FROM_INT uses multiplication",
    "SRS-004-SHALL-030": "Arithmetic wrappers — overflow detection",
    "SRS-004-SHALL-031": "Comparison operators — deterministic ordering",
    "SRS-004-SHALL-032": "Policy serialisation — JCS canonical form",
    
    # Agent Integration (SHALL-033, SHALL-034, SHALL-039)
    "SRS-004-SHALL-033": "Agent integration — observations flow to L5 FSM",
    "SRS-004-SHALL-034": "Agent binding — ledger_seq links observation to agent state",
    "SRS-004-SHALL-039": "Derivation hash — multi-oracle chaining support",
    
    # Truncation (SHALL-040, SHALL-041)
    "SRS-004-SHALL-040": "Truncation size — output_size records original size",
    "SRS-004-SHALL-041": "Atomic output — no partial observations",
}


def find_anchors(root: Path) -> dict:
    """Find all SRS-004 anchors in source files."""
    anchors = {}
    pattern = re.compile(r'SRS-004-SHALL-\d{3}')
    
    for ext in ['*.c', '*.h', '*.py']:
        for path in root.rglob(ext):
            # Skip build directories
            if 'build' in str(path):
                continue
            try:
                content = path.read_text()
                for match in pattern.findall(content):
                    if match not in anchors:
                        anchors[match] = []
                    rel_path = str(path.relative_to(root))
                    if rel_path not in anchors[match]:
                        anchors[match].append(rel_path)
            except Exception:
                pass
    
    return anchors


def verify_rtm(root: Path, verbose: bool = False) -> bool:
    """Verify requirements traceability matrix."""
    print("=" * 70)
    print("ax-rtm-verify.py — SRS-004 v0.3 Traceability Check")
    print("Axioma Oracle Boundary Gateway (L3)")
    print("=" * 70)
    print()
    
    anchors = find_anchors(root)
    
    missing = []
    covered = []
    
    for req_id in sorted(SRS_004_REQUIREMENTS.keys()):
        desc = SRS_004_REQUIREMENTS[req_id]
        if req_id in anchors:
            covered.append(req_id)
            if verbose:
                files = ", ".join(anchors[req_id][:3])
                if len(anchors[req_id]) > 3:
                    files += f" (+{len(anchors[req_id]) - 3} more)"
                print(f"  [COVERED] {req_id}: {files}")
        else:
            missing.append(req_id)
            print(f"  [MISSING] {req_id}: {desc}")
    
    print()
    print("-" * 70)
    print(f"Coverage: {len(covered)}/{len(SRS_004_REQUIREMENTS)} requirements")
    print("-" * 70)
    
    if missing:
        print(f"\nNON-CONFORMANT: {len(missing)} requirements without code anchors")
        print("\nMissing requirements:")
        for req_id in missing:
            print(f"  - {req_id}: {SRS_004_REQUIREMENTS[req_id]}")
        return False
    else:
        print("\nCONFORMANT: All 48 requirements have code anchors")
        return True


def main():
    parser = argparse.ArgumentParser(
        description="Requirements Traceability Matrix Verification for SRS-004"
    )
    parser.add_argument(
        "--root", type=Path, default=Path("."),
        help="Root directory to scan (default: current directory)"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Show covered requirements with file locations"
    )
    args = parser.parse_args()
    
    if not args.root.exists():
        print(f"Error: {args.root} does not exist")
        sys.exit(1)
    
    success = verify_rtm(args.root, args.verbose)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
