"""Check captured QEMU pipeline output; no third-party dependencies required."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--stress", action="store_true")
args = parser.parse_args()
text = args.log.read_text()
for name in ("acquisition", "monitoring", "reporting"):
    assert f"THREAD {name} started" in text, f"Missing {name} startup"
assert not re.search(r"ASSERTION FAIL|FATAL ERROR|Stack overflow|ZEPHYR FATAL", text), text[-2000:]
assert "QEMU: Terminated" in text, "QEMU did not exit cleanly"

summaries = []
for line in text.splitlines():
    # QEMU's interactive exit can interrupt the final console write mid-line.
    # Only that explicitly terminated line is excluded from snapshot checks.
    if "QEMU: Terminated" in line:
        continue
    if line.startswith("SUMMARY "):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        for name in ("attempts", "enqueued", "dropped", "missing", "processed", "timeouts", "gaps"):
            fields[name] = int(fields[name])
        assert fields["attempts"] == fields["enqueued"] + fields["dropped"] + fields["missing"], line
        assert fields["processed"] <= fields["enqueued"], line
        assert fields["report_drops"] == "0/0", line
        summaries.append(fields)
assert summaries, "No periodic summaries"
assert max(s["processed"] for s in summaries) >= 50, "Run for at least a complete scenario cycle"
assert max(s["missing"] for s in summaries) >= 8, "Missing-input phase not observed"
assert max(s["timeouts"] for s in summaries) >= 3, "Timeout debounce not observed"

states = re.findall(r"STATE time=(\d+) ms from=(\w+) to=(\w+) reason=(\w+) changed=(\d+)", text)
assert any(s[1:4] == ("MONITORING", "WARNING", "none") for s in states)
assert any(s[1:4] == ("WARNING", "FAULT", "pressure_threshold") for s in states)
assert any(s[2:4] == ("FAULT", "invalid_sample") for s in states)
assert any(s[2:4] == ("FAULT", "sensor_timeout") for s in states)
assert any(int(s[0]) >= 28000 and s[1:4] == ("FAULT", "MONITORING", "none") for s in states)
readings = re.findall(r"READING seq=(\d+) sample_time=(\d+) ms raw_pressure=(-?\d+) Pa filtered_pressure=(-?\d+) Pa", text)
assert len(readings) >= 8, "Missing processed readings"
assert any(r[2] != r[3] for r in readings), "Filtering not visible"
assert {"normal", "elevated", "critical", "invalid", "out_of_range", "frozen", "timeout", "recovery"}.issubset(
    {s["acq_scenario"] for s in summaries}), "Not all scenarios reported"
if args.stress:
    assert "TEST queue_overflow" in text
    assert "policy=drop_newest" in text
    assert max(s["dropped"] for s in summaries) > 0
    assert max(s["gaps"] for s in summaries) > 8
else:
    assert all(s["dropped"] == 0 for s in summaries)
    assert "TEST queue_overflow" not in text
print(f"Verified {'stress' if args.stress else 'normal'} pipeline: startup, counters, filtering, transitions, timeout and recovery")
