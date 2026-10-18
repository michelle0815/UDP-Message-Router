#!/usr/bin/env python3
"""Run the five assignment acceptance cases against a local router process."""

import select
import signal
import subprocess
import sys
import time


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: python3 test_router.py ROUTER SENDER", file=sys.stderr)
        return 2

    router = subprocess.Popen(
        [sys.argv[1]],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    sender_result = None
    try:
        ready, _, _ = select.select([router.stdout], [], [], 5.0)
        if not ready:
            raise RuntimeError("router did not report that it started within 5 seconds")
        startup_line = router.stdout.readline()
        if "listening on port 9876" not in startup_line:
            raise RuntimeError(f"unexpected router startup output: {startup_line.rstrip()}")

        sender_result = subprocess.run(
            [sys.argv[2]], capture_output=True, text=True, timeout=12.0, check=False
        )
        if sender_result.returncode != 0:
            raise RuntimeError(
                "sender failed:\n" + sender_result.stdout + sender_result.stderr
            )
        time.sleep(0.2)
    finally:
        if router.poll() is None:
            router.send_signal(signal.SIGINT)
        try:
            router_stdout, router_stderr = router.communicate(timeout=3.0)
        except subprocess.TimeoutExpired:
            router.kill()
            router_stdout, router_stderr = router.communicate()
            raise RuntimeError("router did not shut down after SIGINT")

    if router.returncode != 0:
        raise RuntimeError(f"router exited with status {router.returncode}: {router_stderr}")

    output = startup_line + router_stdout
    expected_outputs = {
        "Test 1 routes to Application 1": "Application 1: app_id=1, command_id=101",
        "Test 2 routes to Application 2": "Application 2: app_id=2, command_id=202",
        "Test 4 accepts initial message": "Application 1: app_id=1, command_id=401",
        "Test 5 accepts after one second": "Application 2: app_id=2, command_id=403",
    }
    failures = []
    for description, expected in expected_outputs.items():
        if expected not in output:
            failures.append(description)
    if "params=[0,1,2,3" not in output or ",255]" not in output:
        failures.append("application output includes the complete parameter array")
    if "Application 1: app_id=1, command_id=402" in output:
        failures.append("Test 4 discards the message inside the rate window")
    if "command_id=303" in output:
        failures.append("Test 3 does not route unsupported messages to an application")
    if "discarded unsupported app_id=99" not in router_stderr:
        failures.append("Test 3 reports and discards unsupported app_id")
    if "discarded rate-limited message" not in router_stderr:
        failures.append("Test 4 reports the rate-limited message")

    if failures:
        print("FAIL: " + "; ".join(failures), file=sys.stderr)
        print("--- router stdout ---", file=sys.stderr)
        print(output, file=sys.stderr)
        print("--- router stderr ---", file=sys.stderr)
        print(router_stderr, file=sys.stderr)
        return 1

    for case in (
        "APP_ID_1 routing",
        "APP_ID_2 routing",
        "unsupported app_id discard",
        "rate-limit discard",
        "acceptance after the one-second interval",
    ):
        print(f"PASS: {case}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
