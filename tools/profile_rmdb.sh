#!/usr/bin/env bash
set -euo pipefail

MODE="all"
ROWS=3000
BUILD_DIR="build-profile"
OUT_DIR=""
PORT=18765
HOST="127.0.0.1"
NO_BUILD=0
SAMPLE_FREQUENCY=99
PERF_EVENT="${PERF_EVENT:-cpu-clock}"
PERF_CALL_GRAPH="${PERF_CALL_GRAPH:-fp}"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-}"
KEEP_SERVER_LOG=1
MIN_PROFILE_SECONDS="${MIN_PROFILE_SECONDS:-1.0}"
MAX_PROFILE_ITERATIONS="${MAX_PROFILE_ITERATIONS:-50}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CLIENT_PY=""
RMDB_BIN=""
ACTIVE_SERVER_PID=""
ACTIVE_PERF_PID=""

usage() {
    cat <<'EOF'
Usage: tools/profile_rmdb.sh [options]

Options:
  --mode all|index-single|index-multi|inlj
      Benchmark scenario to run. Default: all.
  --rows N
      Deterministic row/query count. Default: 3000.
  --build-dir DIR
      CMake build directory. Default: build-profile.
  --out DIR
      Output directory. Default: perf_runs/<timestamp>.
  --port PORT
      RMDB server port. Default: 18765.
  --no-build
      Reuse an existing build instead of running CMake.
  --sample-frequency N
      perf sample frequency. Default: 99.
  --event EVENT
      perf event to sample. Default: cpu-clock.
  --call-graph fp|dwarf|lbr|no
      perf call graph mode. Default: fp.
  --flamegraph-dir DIR
      Directory containing stackcollapse-perf.pl and flamegraph.pl.
      If omitted, tools/FlameGraph is cloned automatically when missing.
  --keep-server-log
      Retain server.log. This is the default; the flag is accepted for clarity.
  -h, --help
      Show this help.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

info() {
    echo "[profile-rmdb] $*" >&2
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "${REPO_ROOT}/$1" ;;
    esac
}

cleanup() {
    if [[ -n "${ACTIVE_PERF_PID}" ]] && kill -0 "${ACTIVE_PERF_PID}" >/dev/null 2>&1; then
        kill -INT "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
        wait "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
    fi
    if [[ -n "${ACTIVE_SERVER_PID}" ]] && kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
        kill -INT "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        sleep 0.5
        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill -TERM "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
        wait "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)
            [[ $# -ge 2 ]] || die "--mode requires a value"
            MODE="$2"
            shift 2
            ;;
        --rows)
            [[ $# -ge 2 ]] || die "--rows requires a value"
            ROWS="$2"
            shift 2
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || die "--build-dir requires a value"
            BUILD_DIR="$2"
            shift 2
            ;;
        --out)
            [[ $# -ge 2 ]] || die "--out requires a value"
            OUT_DIR="$2"
            shift 2
            ;;
        --port)
            [[ $# -ge 2 ]] || die "--port requires a value"
            PORT="$2"
            shift 2
            ;;
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --sample-frequency)
            [[ $# -ge 2 ]] || die "--sample-frequency requires a value"
            SAMPLE_FREQUENCY="$2"
            shift 2
            ;;
        --event)
            [[ $# -ge 2 ]] || die "--event requires a value"
            PERF_EVENT="$2"
            shift 2
            ;;
        --call-graph)
            [[ $# -ge 2 ]] || die "--call-graph requires a value"
            PERF_CALL_GRAPH="$2"
            shift 2
            ;;
        --flamegraph-dir)
            [[ $# -ge 2 ]] || die "--flamegraph-dir requires a value"
            FLAMEGRAPH_DIR="$2"
            shift 2
            ;;
        --keep-server-log)
            KEEP_SERVER_LOG=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

case "${MODE}" in
    all|index-single|index-multi|inlj) ;;
    *) die "unsupported --mode '${MODE}'" ;;
esac

[[ "${ROWS}" =~ ^[0-9]+$ ]] || die "--rows must be a positive integer"
[[ "${ROWS}" -gt 0 ]] || die "--rows must be greater than zero"
[[ "${PORT}" =~ ^[0-9]+$ ]] || die "--port must be a positive integer"
[[ "${SAMPLE_FREQUENCY}" =~ ^[0-9]+$ ]] || die "--sample-frequency must be a positive integer"
case "${PERF_CALL_GRAPH}" in
    fp|dwarf|lbr|no) ;;
    *) die "--call-graph must be one of: fp, dwarf, lbr, no" ;;
esac

if [[ -z "${OUT_DIR}" ]]; then
    OUT_DIR="perf_runs/$(date +%Y%m%d_%H%M%S)"
fi

BUILD_DIR="$(abs_path "${BUILD_DIR}")"
OUT_DIR="$(abs_path "${OUT_DIR}")"
CLIENT_PY="${OUT_DIR}/rmdb_tcp_client.py"

case "$(uname -s)" in
    Linux*) ;;
    *) die "this script uses Linux perf; run it inside Linux or WSL2" ;;
esac

require_cmd cmake
require_cmd python3
require_cmd perf
require_cmd perl

mkdir -p "${OUT_DIR}"

METRICS_CSV="${OUT_DIR}/metrics.csv"
SUMMARY_MD="${OUT_DIR}/summary.md"
printf 'scenario,phase,rows,elapsed_seconds,ratio_percent,status,flamegraph\n' > "${METRICS_CSV}"

check_perf_hint() {
    if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
        local paranoid
        paranoid="$(cat /proc/sys/kernel/perf_event_paranoid)"
        if [[ "${EUID}" -ne 0 && "${paranoid}" =~ ^[0-9]+$ && "${paranoid}" -gt 2 ]]; then
            cat >&2 <<EOF
perf_event_paranoid=${paranoid} may block perf sampling for non-root users.
Run one of these in Linux/WSL2, then retry:
  sudo sysctl kernel.perf_event_paranoid=1
  sudo sh -c 'echo 1 > /proc/sys/kernel/perf_event_paranoid'
EOF
        fi
    fi
}

ensure_flamegraph() {
    if [[ -n "${FLAMEGRAPH_DIR}" ]]; then
        FLAMEGRAPH_DIR="$(abs_path "${FLAMEGRAPH_DIR}")"
    else
        FLAMEGRAPH_DIR="${SCRIPT_DIR}/FlameGraph"
        if [[ ! -d "${FLAMEGRAPH_DIR}" ]]; then
            require_cmd git
            info "cloning Brendan Gregg FlameGraph into ${FLAMEGRAPH_DIR}"
            git clone --depth 1 https://github.com/brendangregg/FlameGraph.git "${FLAMEGRAPH_DIR}"
        fi
    fi

    [[ -f "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" ]] ||
        die "missing ${FLAMEGRAPH_DIR}/stackcollapse-perf.pl"
    [[ -f "${FLAMEGRAPH_DIR}/flamegraph.pl" ]] ||
        die "missing ${FLAMEGRAPH_DIR}/flamegraph.pl"
}

build_rmdb() {
    if [[ "${NO_BUILD}" -eq 1 ]]; then
        info "skipping build because --no-build was provided"
    else
        info "configuring RelWithDebInfo build in ${BUILD_DIR}"
        cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -fno-omit-frame-pointer -DNDEBUG" \
            -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -g -fno-omit-frame-pointer -DNDEBUG"

        local jobs
        jobs="$(nproc 2>/dev/null || printf '4')"
        info "building rmdb with ${jobs} job(s)"
        cmake --build "${BUILD_DIR}" --target rmdb -j "${jobs}"
    fi

    RMDB_BIN="${BUILD_DIR}/bin/rmdb"
    [[ -x "${RMDB_BIN}" ]] || die "rmdb binary not found or not executable: ${RMDB_BIN}"
}

write_client() {
    cat > "${CLIENT_PY}" <<'PY'
#!/usr/bin/env python3
import argparse
import socket
import sys
import time


def split_sql(text):
    statements = []
    buf = []
    in_string = False
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "'":
            in_string = not in_string
            buf.append(ch)
        elif ch == ";" and not in_string:
            statement = "".join(buf).strip()
            if statement:
                statements.append(statement + ";")
            buf = []
        else:
            buf.append(ch)
        i += 1
    tail = "".join(buf).strip()
    if tail:
        statements.append(tail)
    return statements


def strip_comment_lines(text):
    lines = []
    for line in text.splitlines():
        if line.strip().startswith("--"):
            continue
        lines.append(line)
    return "\n".join(lines)


def recv_response(sock):
    chunks = []
    total = 0
    while True:
        data = sock.recv(65536)
        if not data:
            raise RuntimeError("server closed the connection before response terminator")
        marker = data.find(b"\0")
        if marker >= 0:
            chunks.append(data[:marker])
            total += marker
            break
        chunks.append(data)
        total += len(data)
    return b"".join(chunks), total


def connect_with_retry(host, port, attempts, delay):
    last_error = None
    for _ in range(attempts):
        try:
            sock = socket.create_connection((host, port), timeout=5.0)
            sock.settimeout(None)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(delay)
    raise RuntimeError(f"could not connect to {host}:{port}: {last_error}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--sql", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--phase", required=True)
    parser.add_argument("--responses", default="")
    parser.add_argument("--connect-attempts", type=int, default=100)
    parser.add_argument("--connect-delay", type=float, default=0.05)
    args = parser.parse_args()

    with open(args.sql, "r", encoding="utf-8") as f:
        statements = split_sql(strip_comment_lines(f.read()))

    response_file = None
    if args.responses:
        response_file = open(args.responses, "w", encoding="utf-8", errors="replace")

    start = time.perf_counter()
    sock = connect_with_retry(args.host, args.port, args.connect_attempts, args.connect_delay)
    try:
        with open(args.log, "a", encoding="utf-8") as log:
            log.write(f"# phase={args.phase} sql={args.sql} statements={len(statements)}\n")
            for idx, statement in enumerate(statements, start=1):
                stmt_start = time.perf_counter()
                sock.sendall(statement.encode("utf-8") + b"\0")
                response, response_bytes = recv_response(sock)
                stmt_elapsed = time.perf_counter() - stmt_start
                preview = " ".join(statement.split())[:160]
                log.write(f"{idx},{stmt_elapsed:.9f},{response_bytes},{preview}\n")
                if response_file is not None:
                    response_file.write(f"-- response {idx}: {preview}\n")
                    response_file.write(response.decode("utf-8", errors="replace"))
                    if not response.endswith(b"\n"):
                        response_file.write("\n")
    finally:
        sock.close()
        if response_file is not None:
            response_file.close()

    elapsed = time.perf_counter() - start
    print(f"{elapsed:.9f}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"client error: {exc}", file=sys.stderr)
        sys.exit(1)
PY
    chmod +x "${CLIENT_PY}"
}

wait_for_server() {
    local port="$1"
    python3 - "${HOST}" "${port}" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
last_error = None
for _ in range(200):
    try:
        sock = socket.create_connection((host, port), timeout=0.2)
        sock.close()
        sys.exit(0)
    except OSError as exc:
        last_error = exc
        time.sleep(0.05)
print(f"server did not accept connections on {host}:{port}: {last_error}", file=sys.stderr)
sys.exit(1)
PY
}

start_server() {
    local scenario_dir="$1"
    local db_name="$2"
    local server_log="$3"

    info "starting rmdb server for ${db_name} on port ${PORT}"
    (
        cd "${scenario_dir}"
        RMDB_PORT="${PORT}" "${RMDB_BIN}" "${db_name}"
    ) > "${server_log}" 2>&1 &
    ACTIVE_SERVER_PID="$!"

    sleep 0.2
    if ! kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
        cat "${server_log}" >&2 || true
        die "rmdb server exited during startup"
    fi
    wait_for_server "${PORT}"
}

stop_server() {
    local pid="${ACTIVE_SERVER_PID}"
    [[ -n "${pid}" ]] || return 0

    if kill -0 "${pid}" >/dev/null 2>&1; then
        kill -INT "${pid}" >/dev/null 2>&1 || true
        for _ in $(seq 1 50); do
            if ! kill -0 "${pid}" >/dev/null 2>&1; then
                break
            fi
            sleep 0.1
        done
        if kill -0 "${pid}" >/dev/null 2>&1; then
            kill -TERM "${pid}" >/dev/null 2>&1 || true
        fi
        wait "${pid}" >/dev/null 2>&1 || true
    fi
    ACTIVE_SERVER_PID=""
}

safe_reset_db_dir() {
    local scenario_dir="$1"
    local db_name="$2"
    local db_dir="${scenario_dir}/${db_name}"
    case "${db_dir}" in
        "${OUT_DIR}"/*) rm -rf -- "${db_dir}" ;;
        *) die "refusing to remove unexpected database directory: ${db_dir}" ;;
    esac
}

run_client() {
    local sql_file="$1"
    local log_file="$2"
    local phase="$3"
    local responses="${4:-}"

    if [[ -n "${responses}" ]]; then
        python3 "${CLIENT_PY}" --host "${HOST}" --port "${PORT}" --sql "${sql_file}" \
            --log "${log_file}" --phase "${phase}" --responses "${responses}"
    else
        python3 "${CLIENT_PY}" --host "${HOST}" --port "${PORT}" --sql "${sql_file}" \
            --log "${log_file}" --phase "${phase}"
    fi
}

perf_failed_message() {
    local perf_log="$1"
    cat "${perf_log}" >&2 || true
    cat >&2 <<'EOF'

perf record failed. Common fixes in Linux/WSL2:
  sudo sysctl kernel.perf_event_paranoid=1
  sudo sh -c 'echo 1 > /proc/sys/kernel/perf_event_paranoid'

If perf is unavailable in this WSL kernel, install/use a WSL kernel with perf support,
or run the script on a native Linux machine.
EOF
    exit 1
}

flamegraph_failed_message() {
    local perf_log="$1"
    local perf_script="$2"
    local folded="$3"
    cat "${perf_log}" >&2 || true
    cat >&2 <<EOF

FlameGraph generation failed because perf produced no collapsible user-space stacks.
Current sampling settings:
  PERF_EVENT=${PERF_EVENT}
  PERF_CALL_GRAPH=${PERF_CALL_GRAPH}

Useful checks:
  perf report -i <phase>.perf.data --stdio | head -80
  head -80 ${perf_script}
  wc -l ${folded}

Try one of these reruns:
  PERF_CALL_GRAPH=dwarf bash tools/profile_rmdb.sh ...
  PERF_EVENT=task-clock PERF_CALL_GRAPH=fp bash tools/profile_rmdb.sh ...
  sudo sysctl kernel.perf_event_paranoid=1
EOF
    exit 1
}

write_no_stacks_svg() {
    local svg="$1"
    local phase="$2"
    cat > "${svg}" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="960" height="160" viewBox="0 0 960 160">
  <rect width="960" height="160" fill="#f8f8f8"/>
  <text x="24" y="48" font-family="Arial, sans-serif" font-size="22" fill="#333">No collapsible stacks captured</text>
  <text x="24" y="82" font-family="Arial, sans-serif" font-size="15" fill="#555">phase: ${phase}</text>
  <text x="24" y="108" font-family="Arial, sans-serif" font-size="15" fill="#555">perf data/script were kept for inspection. This often happens when the indexed phase is too fast in WSL.</text>
  <text x="24" y="132" font-family="Arial, sans-serif" font-size="15" fill="#555">Try larger --rows, higher MIN_PROFILE_SECONDS, or run on native Linux for fuller stacks.</text>
</svg>
EOF
}

generate_flamegraph() {
    local scenario_dir="$1"
    local phase="$2"
    local perf_data="${scenario_dir}/${phase}.perf.data"
    local perf_script="${scenario_dir}/${phase}.perf.script"
    local folded="${scenario_dir}/${phase}.folded.stacks"
    local svg="${scenario_dir}/${phase}.flamegraph.svg"
    local perf_log="${scenario_dir}/${phase}.perf.log"

    perf script -i "${perf_data}" > "${perf_script}" 2>> "${perf_log}" || perf_failed_message "${perf_log}"
    perl "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" "${perf_script}" > "${folded}" 2>> "${perf_log}" ||
        perf_failed_message "${perf_log}"
    if [[ ! -s "${folded}" ]]; then
        {
            echo "warning: no collapsible stacks for ${phase}; writing placeholder SVG"
            echo "PERF_EVENT=${PERF_EVENT}"
            echo "PERF_CALL_GRAPH=${PERF_CALL_GRAPH}"
        } >> "${perf_log}"
        write_no_stacks_svg "${svg}" "${phase}"
        return 0
    fi
    perl "${FLAMEGRAPH_DIR}/flamegraph.pl" "${folded}" > "${svg}" 2>> "${perf_log}" ||
        flamegraph_failed_message "${perf_log}" "${perf_script}" "${folded}"
}

profile_phase() {
    local scenario_dir="$1"
    local scenario="$2"
    local phase="$3"
    local sql_file="$4"
    local client_log="$5"
    local responses="${6:-}"

    local perf_data="${scenario_dir}/${phase}.perf.data"
    local perf_log="${scenario_dir}/${phase}.perf.log"
    local elapsed
    local total_elapsed="0"
    local iterations=0

    info "profiling ${scenario}/${phase}"
    perf record -F "${SAMPLE_FREQUENCY}" -e "${PERF_EVENT}" --call-graph "${PERF_CALL_GRAPH}" \
        -p "${ACTIVE_SERVER_PID}" \
        -o "${perf_data}" -- sleep 86400 > "${perf_log}" 2>&1 &
    ACTIVE_PERF_PID="$!"

    sleep 0.5
    if ! kill -0 "${ACTIVE_PERF_PID}" >/dev/null 2>&1; then
        wait "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
        ACTIVE_PERF_PID=""
        perf_failed_message "${perf_log}"
    fi

    while true; do
        iterations=$((iterations + 1))
        if ! elapsed="$(run_client "${sql_file}" "${client_log}" "${phase}_iter${iterations}" "${responses}")"; then
            kill -INT "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
            wait "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
            ACTIVE_PERF_PID=""
            die "client failed during ${scenario}/${phase}; see ${client_log} and ${scenario_dir}/server.log"
        fi
        total_elapsed="$(python3 - "${total_elapsed}" "${elapsed}" <<'PY'
import sys
print(f"{float(sys.argv[1]) + float(sys.argv[2]):.9f}")
PY
)"
        if python3 - "${total_elapsed}" "${MIN_PROFILE_SECONDS}" "${iterations}" "${MAX_PROFILE_ITERATIONS}" <<'PY'
import sys
total = float(sys.argv[1])
minimum = float(sys.argv[2])
iterations = int(sys.argv[3])
maximum = int(sys.argv[4])
sys.exit(0 if total >= minimum or iterations >= maximum else 1)
PY
        then
            break
        fi
    done

    kill -INT "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
    wait "${ACTIVE_PERF_PID}" >/dev/null 2>&1 || true
    ACTIVE_PERF_PID=""

    generate_flamegraph "${scenario_dir}" "${phase}"
    {
        printf '# profile_phase=%s iterations=%s total_elapsed=%s min_profile_seconds=%s\n' \
            "${phase}" "${iterations}" "${total_elapsed}" "${MIN_PROFILE_SECONDS}"
    } >> "${client_log}"
    python3 - "${total_elapsed}" "${iterations}" <<'PY'
import sys
print(f"{float(sys.argv[1]) / int(sys.argv[2]):.9f}")
PY
}

append_metric() {
    local scenario="$1"
    local phase="$2"
    local elapsed="$3"
    local ratio="$4"
    local status="$5"
    local flamegraph="$6"
    printf '%s,%s,%s,%s,%s,%s,%s\n' \
        "${scenario}" "${phase}" "${ROWS}" "${elapsed}" "${ratio}" "${status}" "${flamegraph}" >> "${METRICS_CSV}"
}

ratio_percent() {
    python3 - "$1" "$2" <<'PY'
import sys
before = float(sys.argv[1])
after = float(sys.argv[2])
print("inf" if before <= 0 else f"{(after / before) * 100.0:.2f}")
PY
}

ratio_status() {
    local ratio="$1"
    python3 - "${ratio}" <<'PY'
import math
import sys
ratio = float(sys.argv[1])
print("PASS" if math.isfinite(ratio) and ratio <= 70.0 else "FAIL")
PY
}

write_index_single_sql() {
    local scenario_dir="$1"
    local setup_sql="${scenario_dir}/setup.sql"
    local pre_sql="${scenario_dir}/profile_pre_index.sql"
    local post_sql="${scenario_dir}/profile_post_index.sql"

    {
        echo "create table warehouse (w_id int, name char(8));"
        for i in $(seq 1 "${ROWS}"); do
            printf "insert into warehouse values (%d, 'n%07d');\n" "${i}" "${i}"
        done
    } > "${setup_sql}"

    {
        echo "-- profile: no index"
        for i in $(seq 1 "${ROWS}"); do
            printf "select * from warehouse where w_id = %d;\n" "${i}"
        done
    } > "${pre_sql}"

    cp "${pre_sql}" "${post_sql}"
    echo "create index warehouse(w_id);" > "${scenario_dir}/create_index.sql"
    cat "${pre_sql}" "${scenario_dir}/create_index.sql" "${post_sql}" > "${scenario_dir}/profile.sql"
}

write_index_multi_sql() {
    local scenario_dir="$1"
    local setup_sql="${scenario_dir}/setup.sql"
    local pre_sql="${scenario_dir}/profile_pre_index.sql"
    local post_sql="${scenario_dir}/profile_post_index.sql"

    {
        echo "create table warehouse (w_id int, name char(8), flo float);"
        for i in $(seq 1 "${ROWS}"); do
            printf "insert into warehouse values (%d, 'm%07d', %d.500000);\n" "${i}" "${i}" "${i}"
        done
    } > "${setup_sql}"

    {
        echo "-- profile: no composite index"
        for i in $(seq 1 "${ROWS}"); do
            printf "select * from warehouse where w_id = %d and flo = %d.500000;\n" "${i}" "${i}"
        done
    } > "${pre_sql}"

    cp "${pre_sql}" "${post_sql}"
    echo "create index warehouse(w_id,flo);" > "${scenario_dir}/create_index.sql"
    cat "${pre_sql}" "${scenario_dir}/create_index.sql" "${post_sql}" > "${scenario_dir}/profile.sql"
}

write_inlj_sql() {
    local scenario_dir="$1"
    local setup_sql="${scenario_dir}/setup.sql"
    local pre_sql="${scenario_dir}/profile_pre_index.sql"
    local post_sql="${scenario_dir}/profile_post_index.sql"
    local explain_sql="${scenario_dir}/explain.sql"
    local join_sql
    join_sql="select departments.dept_name, employees.emp_name from departments join employees on departments.dept_id = employees.dept_id;"

    {
        echo "create table departments (dept_id int, dept_name char(8));"
        echo "create table employees (emp_id int, dept_id int, emp_name char(8));"
        for i in $(seq 1 "${ROWS}"); do
            printf "insert into departments values (%d, 'd%07d');\n" "${i}" "${i}"
            printf "insert into employees values (%d, %d, 'e%07d');\n" "${i}" "${i}" "${i}"
        done
    } > "${setup_sql}"

    printf '%s\n' "${join_sql}" > "${pre_sql}"
    cp "${pre_sql}" "${post_sql}"
    echo "create index employees(dept_id);" > "${scenario_dir}/create_index.sql"
    printf 'explain analyze %s\n' "${join_sql}" > "${explain_sql}"
    cat "${pre_sql}" "${scenario_dir}/create_index.sql" "${post_sql}" "${explain_sql}" > "${scenario_dir}/profile.sql"
}

run_index_scenario() {
    local scenario="$1"
    local writer="$2"
    local scenario_dir="${OUT_DIR}/${scenario}"
    local db_name="profile_${scenario//-/_}"
    local client_log="${scenario_dir}/client.log"
    local server_log="${scenario_dir}/server.log"
    local pre_elapsed
    local post_elapsed
    local ratio
    local status

    mkdir -p "${scenario_dir}"
    safe_reset_db_dir "${scenario_dir}" "${db_name}"
    "${writer}" "${scenario_dir}"

    start_server "${scenario_dir}" "${db_name}" "${server_log}"
    run_client "${scenario_dir}/setup.sql" "${client_log}" "setup" >/dev/null
    pre_elapsed="$(profile_phase "${scenario_dir}" "${scenario}" "pre_index" "${scenario_dir}/profile_pre_index.sql" "${client_log}")"
    run_client "${scenario_dir}/create_index.sql" "${client_log}" "create_index" >/dev/null
    post_elapsed="$(profile_phase "${scenario_dir}" "${scenario}" "post_index" "${scenario_dir}/profile_post_index.sql" "${client_log}")"
    stop_server

    if [[ -f "${scenario_dir}/${db_name}/output.txt" ]]; then
        cp "${scenario_dir}/${db_name}/output.txt" "${scenario_dir}/output.txt"
    fi
    cp "${scenario_dir}/post_index.perf.data" "${scenario_dir}/perf.data"
    cp "${scenario_dir}/post_index.flamegraph.svg" "${scenario_dir}/flamegraph.svg"

    ratio="$(ratio_percent "${pre_elapsed}" "${post_elapsed}")"
    status="$(ratio_status "${ratio}")"
    append_metric "${scenario}" "pre_index" "${pre_elapsed}" "" "BASELINE" "${scenario}/pre_index.flamegraph.svg"
    append_metric "${scenario}" "post_index" "${post_elapsed}" "${ratio}" "${status}" "${scenario}/post_index.flamegraph.svg"
}

run_inlj_scenario() {
    local scenario="inlj"
    local scenario_dir="${OUT_DIR}/${scenario}"
    local db_name="profile_inlj"
    local client_log="${scenario_dir}/client.log"
    local server_log="${scenario_dir}/server.log"
    local pre_elapsed
    local post_elapsed
    local ratio
    local ratio_result
    local plan_result
    local status

    mkdir -p "${scenario_dir}"
    safe_reset_db_dir "${scenario_dir}" "${db_name}"
    write_inlj_sql "${scenario_dir}"

    start_server "${scenario_dir}" "${db_name}" "${server_log}"
    run_client "${scenario_dir}/setup.sql" "${client_log}" "setup" >/dev/null
    pre_elapsed="$(profile_phase "${scenario_dir}" "${scenario}" "pre_index" "${scenario_dir}/profile_pre_index.sql" "${client_log}")"
    run_client "${scenario_dir}/explain.sql" "${client_log}" "explain_pre" "${scenario_dir}/explain_pre.txt" >/dev/null
    run_client "${scenario_dir}/create_index.sql" "${client_log}" "create_index" >/dev/null
    post_elapsed="$(profile_phase "${scenario_dir}" "${scenario}" "post_index" "${scenario_dir}/profile_post_index.sql" "${client_log}")"
    run_client "${scenario_dir}/explain.sql" "${client_log}" "explain_post" "${scenario_dir}/explain_post.txt" >/dev/null
    stop_server

    if [[ -f "${scenario_dir}/${db_name}/output.txt" ]]; then
        cp "${scenario_dir}/${db_name}/output.txt" "${scenario_dir}/output.txt"
    fi
    cp "${scenario_dir}/post_index.perf.data" "${scenario_dir}/perf.data"
    cp "${scenario_dir}/post_index.flamegraph.svg" "${scenario_dir}/flamegraph.svg"

    ratio="$(ratio_percent "${pre_elapsed}" "${post_elapsed}")"
    ratio_result="$(ratio_status "${ratio}")"
    if grep -q "type=IndexScan" "${scenario_dir}/explain_post.txt"; then
        plan_result="PASS"
    else
        plan_result="FAIL"
    fi
    if [[ "${ratio_result}" == "PASS" && "${plan_result}" == "PASS" ]]; then
        status="PASS"
    else
        status="FAIL"
    fi

    append_metric "${scenario}" "pre_index" "${pre_elapsed}" "" "BASELINE" "${scenario}/pre_index.flamegraph.svg"
    append_metric "${scenario}" "post_index" "${post_elapsed}" "${ratio}" "${status}" "${scenario}/post_index.flamegraph.svg"
}

write_summary() {
    python3 - "${METRICS_CSV}" "${SUMMARY_MD}" "${MODE}" "${ROWS}" "${OUT_DIR}" "${BUILD_DIR}" \
        "${PORT}" "${SAMPLE_FREQUENCY}" "${MIN_PROFILE_SECONDS}" "${MAX_PROFILE_ITERATIONS}" \
        "${PERF_EVENT}" "${PERF_CALL_GRAPH}" <<'PY'
import csv
import sys

metrics, summary, mode, rows, out_dir, build_dir, port, frequency, min_seconds, max_iterations, event, call_graph = sys.argv[1:]

with open(metrics, newline="", encoding="utf-8") as f:
    rows_data = list(csv.DictReader(f))

with open(summary, "w", encoding="utf-8") as out:
    out.write("# RMDB Profile Summary\n\n")
    out.write(f"- mode: `{mode}`\n")
    out.write(f"- rows: `{rows}`\n")
    out.write(f"- port: `{port}`\n")
    out.write(f"- perf sample frequency: `{frequency}`\n")
    out.write(f"- perf event: `{event}`\n")
    out.write(f"- perf call graph: `{call_graph}`\n")
    out.write(f"- min profile seconds: `{min_seconds}`\n")
    out.write(f"- max profile iterations: `{max_iterations}`\n")
    out.write(f"- build dir: `{build_dir}`\n")
    out.write(f"- output dir: `{out_dir}`\n\n")
    out.write("| Scenario | Phase | Rows | Seconds | Ratio % | Status | FlameGraph |\n")
    out.write("|---|---:|---:|---:|---:|---|---|\n")
    for row in rows_data:
        ratio = row["ratio_percent"] or "-"
        flame = row["flamegraph"]
        out.write(
            f"| {row['scenario']} | {row['phase']} | {row['rows']} | "
            f"{row['elapsed_seconds']} | {ratio} | {row['status']} | `{flame}` |\n"
        )
PY
}

main() {
    check_perf_hint
    ensure_flamegraph
    write_client
    build_rmdb

    case "${MODE}" in
        all)
            run_index_scenario "index-single" "write_index_single_sql"
            run_index_scenario "index-multi" "write_index_multi_sql"
            run_inlj_scenario
            ;;
        index-single)
            run_index_scenario "index-single" "write_index_single_sql"
            ;;
        index-multi)
            run_index_scenario "index-multi" "write_index_multi_sql"
            ;;
        inlj)
            run_inlj_scenario
            ;;
    esac

    write_summary
    info "metrics: ${METRICS_CSV}"
    info "summary: ${SUMMARY_MD}"
}

main "$@"
