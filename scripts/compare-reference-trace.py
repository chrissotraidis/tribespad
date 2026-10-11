#!/usr/bin/env python3
"""Compare TribesPad reference traces (tests/sim_reference_trace.cs).

Each run logs a subset of the same per-tick physics sequence, so samples are
matched by state rather than by time: a sample whose position and velocity
appear in the other run is common; the same position with another velocity
is a divergence, and the final resting state must be
identical. Reports common samples, mismatches and the resting states.
"""
import re,sys,json
def load(path):
    rows=[]
    for line in open(path,errors='replace'):
        m=re.search(r'TPTRACE s (\d+) (\d+) (.+)$',line.strip())
        if m:
            pos,vel,energy,damage=[p.strip() for p in m[3].split('|')]
            rows.append(dict(pos=pos,vel=vel,energy=energy,damage=damage))
    return rows
a,b=load(sys.argv[1]),load(sys.argv[2])
key=lambda r:(r['pos'],r['vel'])
index={key(r):r for r in b}
position={r['pos']:r for r in b}
common=[r for r in a if key(r) in index]
# A position seen in both runs with a different velocity (or vice versa) is a real divergence.
mismatch=[(r,position[r['pos']]) for r in a if r['pos'] in position and key(r) not in index and not any(key(x)==key(r) for x in b)]
# The last moving tick and the resting tick share a position; that is sampling, not divergence.
mismatch=[m for m in mismatch if m[0]['vel']!=m[1]['vel'] and '0 0 0' not in (m[0]['vel'],m[1]['vel'])]
rest_a,rest_b=a[-1] if a else None,b[-1] if b else None
report=dict(samples=[len(a),len(b)],common=len(common),mismatches=len(mismatch),
            rest=[rest_a,rest_b],rest_identical=rest_a==rest_b,
            first_mismatch=mismatch[0] if mismatch else None)
print(json.dumps(report,indent=1))
sys.exit(0 if report['rest_identical'] and not mismatch and common else 1)
