"""VS Code-owned Pintos/QEMU process; terminate it when debugging stops."""

import os
import runpy
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import sys
import time


def main():
    root = Path(__file__).resolve().parent.parent
    project, arguments = sys.argv[1:]
    helper = runpy.run_path(str(root / '.vscode/run_shortcut.py'))
    project = helper['resolve_project'](project)
    if project not in {"threads", "userprog", "vm", "filesys"}:
        raise SystemExit("Unknown pintos.project: " + project)
    try:
        if not isinstance(arguments, str) or not arguments.strip():
            raise ValueError("pintos.debugArgs must be a non-empty string")
        arguments = shlex.split(arguments)
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"Invalid pintos.debugArgs: {error}")
    # Fail rather than accidentally connecting the debugger to another session.
    with socket.socket() as probe:
        try:
            probe.bind(("127.0.0.1", 1234))
        except OSError:
            raise SystemExit("GDB port 1234 is busy. Stop the previous QEMU session.")

    process = subprocess.Popen(
        [sys.executable, "-u", str(root / "pintos/utils/pintos"),
         "--gdb", *arguments],
        cwd=root / "pintos" / project / "build",
        start_new_session=True,
    )

    def stop(signum, frame):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        deadline = time.monotonic() + 15
        while process.poll() is None:
            # Inspect the Linux listener without consuming a GDB connection.
            entries = Path("/proc/net/tcp").read_text().splitlines()[1:]
            if any(row.split()[1].endswith(":04D2") and
                   row.split()[3] == "0A" for row in entries):
                print("PINTOS_GDB_READY", flush=True)
                return process.wait()
            if time.monotonic() >= deadline:
                raise SystemExit("QEMU did not open GDB port 1234 within 15 seconds.")
            time.sleep(0.05)
        raise SystemExit("Pintos exited before opening GDB port 1234.")
    finally:
        try:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        except ProcessLookupError:
            pass


if __name__ == "__main__":
    sys.exit(main())
