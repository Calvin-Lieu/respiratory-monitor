#!/usr/bin/env python3
"""Run a bounded Zephyr QEMU test using the emulator's documented exit key."""
import argparse
import errno
import os
import re
import select
import signal
import subprocess
import sys
import time
import tty
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", required=True)
parser.add_argument("--log", type=Path, required=True)
parser.add_argument("--mode", choices=("unit", "normal", "stress"), required=True)
parser.add_argument("--timeout", type=int, default=180)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error("--timeout must be positive")

master, slave = os.openpty()
tty.setraw(slave)
process = subprocess.Popen(
    ["west", "build", "-d", args.build_dir, "-t", "run"],
    stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
)
os.close(slave)
deadline = time.monotonic() + args.timeout
output = bytearray()
exit_requested = False
problem = None
try:
    while True:
        if time.monotonic() >= deadline:
            problem = "Timed out before QEMU completed the requested scenario"
            break
        ready, _, _ = select.select([master], [], [], max(0.0, min(0.5, deadline - time.monotonic())))
        if ready:
            try:
                chunk = os.read(master, 65536)
            except OSError as exc:
                if exc.errno != errno.EIO:
                    raise
                chunk = b""
            if chunk:
                output.extend(chunk)
                if len(output) > 4 * 1024 * 1024:
                    problem = "QEMU output exceeded the 4 MiB capture limit"
                    break
                if not exit_requested:
                    if args.mode == "unit":
                        complete = b"PROJECT EXECUTION SUCCESSFUL" in output
                    else:
                        required_ms = 80000 if args.mode == "stress" else 40000
                        matches = re.findall(rb"COUNTERS time=(\d+) ms[^\r\n]*[\r\n]", output)
                        complete = any(int(value) >= required_ms for value in matches)
                    if complete:
                        os.write(master, b"\x01x")  # QEMU monitor: Ctrl-A, X.
                        exit_requested = True
            elif process.poll() is not None:
                break
        if process.poll() is not None:
            # Read any remaining PTY output before validating.
            if not ready:
                break
    if process.poll() is None:
        if not exit_requested:
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            problem = problem or "QEMU did not exit after Ctrl-A, X"
finally:
    os.close(master)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_bytes(output)

text = output.decode("utf-8", errors="replace")
if problem or not exit_requested or process.returncode != 0:
    print(text[-5000:], file=sys.stderr)
    sys.exit(problem or f"QEMU ended unexpectedly (status {process.returncode})")
if args.mode == "unit":
    suites = re.findall(
        r"SUITE PASS - .*?\[([^]]+)\]: pass = (\d+), fail = (\d+), skip = (\d+), total = (\d+)", text
    )
    if ({name for name, *_ in suites} != {"filter", "monitor", "ring_buffer", "sensor_health"}
            or sum(int(row[1]) for row in suites) < 15
            or any(int(row[2]) != 0 or int(row[3]) != 0 or row[1] != row[4] for row in suites)
            or "PROJECT EXECUTION FAILED" in text):
        print(text[-5000:], file=sys.stderr)
        sys.exit("ztest suite results were incomplete, failed or skipped")
    print(f"QEMU ztest: {sum(int(row[1]) for row in suites)} passed; 0 failed, 0 skipped")
else:
    command = [sys.executable, str(Path(__file__).with_name("check_pipeline_log.py")), str(args.log)]
    if args.mode == "stress":
        command.append("--stress")
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        print(text[-5000:], file=sys.stderr)
        sys.exit(result.returncode)
