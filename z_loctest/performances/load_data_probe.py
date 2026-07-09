#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
LOG_DIR = Path(__file__).resolve().parent / "logs"


WAREHOUSE_SCHEMA = (
    "create table warehouse ("
    "w_id int,"
    "w_name char(32),"
    "w_street_1 char(32),"
    "w_street_2 char(32),"
    "w_city char(32),"
    "w_state char(8),"
    "w_zip char(16),"
    "w_tax float,"
    "w_ytd float);"
)


def read_rss_kb(pid: int) -> int:
    try:
        with open(f"/proc/{pid}/status", "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def wait_for_server(port: int, timeout: float) -> None:
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2) as sock:
                sock.sendall(b"exit")
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on port {port}: {last_error}")


def send_sql(port: int, sql: str) -> str:
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        sock.sendall(sql.encode("utf-8") + b"\0")
        data = sock.recv(1 << 20)
        sock.sendall(b"exit")
    return data.rstrip(b"\0").decode("utf-8", "replace")


def run_sql(port: int, sql: str, proc: subprocess.Popen, log_file) -> str:
    start = time.perf_counter()
    result = send_sql(port, sql)
    elapsed_ms = (time.perf_counter() - start) * 1000
    entry = {
        "sql": sql,
        "elapsed_ms": round(elapsed_ms, 3),
        "server_rss_kb": read_rss_kb(proc.pid),
        "result_preview": result[:240],
    }
    log_file.write(json.dumps(entry, ensure_ascii=False) + "\n")
    log_file.flush()
    print(f"{elapsed_ms:9.3f} ms | rss={entry['server_rss_kb']:8d} KB | {sql}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe RMDB load command timing and correctness.")
    parser.add_argument("--port", type=int, default=18791)
    parser.add_argument("--db", default="load_probe_db")
    parser.add_argument(
        "--csv",
        default="../../src/test/performance_test/table_data/warehouse.csv",
        help="CSV path as seen from the database directory under build/",
    )
    args = parser.parse_args()

    rmdb = BUILD / "bin" / "rmdb"
    if not rmdb.exists():
        raise RuntimeError("build/bin/rmdb not found; run cmake --build build first")

    db_path = BUILD / args.db
    if db_path.exists():
        shutil.rmtree(db_path)
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / f"load_data_probe_{int(time.time())}.jsonl"

    env = os.environ.copy()
    env["RMDB_PORT"] = str(args.port)
    proc = subprocess.Popen(
        [str(rmdb), args.db],
        cwd=str(BUILD),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(args.port, 5)
        with open(log_path, "w", encoding="utf-8") as log_file:
            run_sql(args.port, "set output_file off", proc, log_file)
            run_sql(args.port, WAREHOUSE_SCHEMA, proc, log_file)
            run_sql(args.port, "create index warehouse(w_id);", proc, log_file)
            run_sql(args.port, f"load {args.csv} into warehouse;", proc, log_file)
            count_output = run_sql(args.port, "select count(*) from warehouse;", proc, log_file)
            point_output = run_sql(
                args.port,
                "select w_name,w_tax,w_ytd from warehouse where w_id = 1;",
                proc,
                log_file,
            )
        if "Total record(s): 1" not in count_output or "JxMJvSF3vi" not in point_output:
            print(f"validation failed; see {log_path}", file=sys.stderr)
            return 1
        print(f"validation passed; log written to {log_path}")
        return 0
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
