"""Check captured QEMU pipeline output; no third-party dependencies required."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--stress", action="store_true")
args = parser.parse_args()
raw_text = args.log.read_text()
# Normalize Zephyr's timestamp/level/module prefix while retaining non-log lines.
text = "\n".join(line.split("respmon: ", 1)[-1] for line in raw_text.splitlines())
for name in ("acquisition", "monitoring", "reporting"):
    assert f"THREAD {name} started" in text, f"Missing {name} startup"
assert not re.search(r"ASSERTION FAIL|FATAL ERROR|Stack overflow|ZEPHYR FATAL", text), text[-2000:]
assert "QEMU: Terminated" in text, "QEMU did not exit cleanly"

summaries = []
counters = []
for line in text.splitlines():
    # QEMU's interactive exit can interrupt the final console write mid-line.
    # Only that explicitly terminated line is excluded from snapshot checks.
    if "QEMU: Terminated" in line:
        continue
    if line.startswith("SUMMARY "):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        required = ("time", "acq_scenario", "state", "reason", "seq", "sample_time",
                    "raw_pressure", "filtered_pressure", "raw_airflow", "filtered_airflow",
                    "valid", "filtered_valid", "latest_event_sample", "total", "valid_count", "invalid", "dropped")
        assert all(key in fields for key in required), line
        for name in ("total", "valid_count", "invalid", "dropped"):
            fields[name] = int(fields[name])
        assert fields["total"] == fields["valid_count"] + fields["invalid"], line
        summaries.append(fields)
    elif line.startswith("COUNTERS "):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        for name in ("attempts", "enqueued", "dropped", "missing", "total", "valid", "invalid", "timeouts", "gaps"):
            fields[name] = int(fields[name])
        assert fields["attempts"] == fields["enqueued"] + fields["dropped"] + fields["missing"], line
        assert fields["total"] == fields["valid"] + fields["invalid"], line
        assert fields["total"] <= fields["enqueued"], line
        assert fields["report_drops"] == "0/0", line
        counters.append(fields)
assert summaries and counters, "Missing periodic summaries or counters"
assert max(s["total"] for s in summaries) >= 70, "Run for at least a complete scenario cycle"
assert max(s["missing"] for s in counters) >= 8, "Missing-input phase not observed"
assert max(s["timeouts"] for s in counters) >= 3, "Timeout debounce not observed"
assert max(s["valid"] for s in counters) > 0 and max(s["invalid"] for s in counters) > 0
assert re.search(r"<inf> respmon: SUMMARY", raw_text)
assert re.search(r"<wrn> respmon: STATE .*to=WARNING", raw_text)
assert re.search(r"<err> respmon: STATE .*to=FAULT", raw_text)
assert re.search(r"<inf> respmon: STACK high_water_bytes acquisition=\d+/1024 monitoring=\d+/2048 reporting=\d+/2048", raw_text)
assert "STACK inspection unavailable" not in raw_text

states = re.findall(r"STATE time=(\d+) ms from=(\w+) to=(\w+) reason=(\w+) changed=(\d+)", text)
assert any(s[1:4] == ("MONITORING", "WARNING", "none") for s in states)
assert any(s[1:4] == ("WARNING", "FAULT", "pressure_threshold") for s in states)
assert any(s[2:4] == ("FAULT", "invalid_sample") for s in states)
for reason in ("out_of_range", "stale_timestamp", "sequence_gap", "frozen_sensor"):
    assert any(s[2:4] == ("FAULT", reason) for s in states), reason
assert any(s[2:4] == ("FAULT", "sensor_timeout") for s in states)
assert any(int(s[0]) >= 36000 and s[1:4] == ("FAULT", "MONITORING", "none") for s in states)
readings = re.findall(r"SUMMARY .*seq=(\d+) sample_time=(\d+) ms raw_pressure=(-?\d+) Pa filtered_pressure=(-?\d+) Pa", text)
assert len(readings) >= 8, "Missing processed readings"
assert any(r[2] != r[3] for r in readings), "Filtering not visible"
assert re.search(r"SUMMARY .*raw_pressure=-1000 Pa filtered_pressure=0 Pa .*valid=1 filtered_valid=0", text), \
    "Source-claimed-valid out-of-range sample was not rejected"
health_reasons = re.findall(r"HEALTH time=\d+ ms reason=(\w+)", text)
assert all(left != right for left, right in zip(health_reasons, health_reasons[1:])), \
    "Repeated unchanged health reason logged"
assert {"normal", "elevated", "critical", "invalid", "out_of_range", "stale_timestamp", "sequence_gap", "frozen", "timeout", "recovery"}.issubset(
    {s["acq_scenario"] for s in summaries}), "Not all scenarios reported"
if args.stress:
    assert "TEST queue_overflow" in text
    assert "policy=drop_newest" in text
    assert max(s["dropped"] for s in summaries) > 0
    assert max(s["gaps"] for s in counters) > 16
else:
    assert all(s["dropped"] == 0 for s in summaries)
    assert "TEST queue_overflow" not in text
print(f"Verified {'stress' if args.stress else 'normal'} pipeline: startup, counters, filtering, transitions, timeout and recovery")
