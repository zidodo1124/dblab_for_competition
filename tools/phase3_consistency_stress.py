#!/usr/bin/env python3
"""Five-warehouse, 16-client Phase 3 consistency and recovery stress test."""

import argparse
import os
import re
import shutil
import socket
import subprocess
import threading
import time


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
INITIAL_ORDERS_PER_DISTRICT = 25
INITIAL_NEW_ORDER_START = 6


class Session:
    def __init__(self, port, timeout=5):
        self.port = port
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)

    def send(self, sql, timeout=30):
        self.sock.sendall(sql.encode("utf-8"))
        self.sock.settimeout(timeout)
        chunks = []
        while True:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("server closed connection before response terminator")
            chunks.append(chunk)
            if chunk.endswith(b"\0"):
                break
        return b"".join(chunks).rstrip(b"\0").decode("utf-8", errors="replace")

    def close(self):
        try:
            self.sock.sendall(b"exit")
        except OSError:
            pass
        self.sock.close()


def failed(response):
    lowered = response.lower()
    return "abort" in lowered or "failure" in lowered or "error" in lowered


def numeric_cells(response):
    values = []
    for line in response.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 1 or not cells[0] or set(cells[0]) <= {"-"}:
            continue
        if cells[0].lower() in {"null", "none"}:
            values.append(None)
            continue
        if re.fullmatch(r"-?\d+(?:\.\d+)?", cells[0]):
            values.append(float(cells[0]))
    return values


def scalar_float(session, sql):
    response = session.send(sql)
    values = numeric_cells(response)
    if len(values) != 1 or values[0] is None:
        raise AssertionError(
            f"expected one numeric value for {sql!r}, got {values}; response:\n{response}"
        )
    return values[0]


def scalar_int(session, sql):
    value = scalar_float(session, sql)
    if value != int(value):
        raise AssertionError(f"expected integer for {sql!r}, got {value}")
    return int(value)


def scalar_optional_int(session, sql):
    values = numeric_cells(session.send(sql))
    if len(values) != 1:
        raise AssertionError(f"expected one scalar value for {sql!r}, got {values}")
    return None if values[0] is None else int(values[0])


class Server:
    def __init__(self, binary, db_path, port, log_path):
        self.binary = binary
        self.db_path = db_path
        self.port = port
        self.log_path = log_path
        self.proc = None
        self.log_file = None

    def start(self, reset=False):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", self.port))
        except OSError as exc:
            raise RuntimeError(f"port {self.port} is already in use; choose another --port or stop its RMDB server") from exc
        finally:
            probe.close()
        if reset:
            shutil.rmtree(self.db_path, ignore_errors=True)
        env = os.environ.copy()
        env["RMDB_PORT"] = str(self.port)
        self.log_file = open(self.log_path, "a", encoding="utf-8")
        self.proc = subprocess.Popen(
            [self.binary, self.db_path],
            cwd=ROOT,
            env=env,
            stdout=self.log_file,
            stderr=subprocess.STDOUT,
        )
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError(f"server exited early; see {self.log_path}")
                time.sleep(0.05)
        raise RuntimeError(f"server did not listen on port {self.port}")

    def crash(self):
        self.proc.kill()
        self.proc.wait(timeout=5)
        self.log_file.close()
        self.proc = None
        self.log_file = None

    def stop(self):
        if self.proc is None:
            return
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        self.log_file.close()
        self.proc = None
        self.log_file = None


def checked(session, sql):
    response = session.send(sql)
    if failed(response):
        raise AssertionError(f"statement failed: {sql}\n{response}")
    return response


def setup_schema(admin):
    statements = [
        "create table warehouse (w_id int, w_ytd float);",
        "create table district (d_id int, d_w_id int, d_ytd float, d_next_o_id int);",
        "create table customer (c_id int, c_d_id int, c_w_id int, c_balance float, "
        "c_ytd_payment float, c_payment_cnt int, c_delivery_cnt int);",
        "create table history (h_id int, h_c_id int, h_c_d_id int, h_c_w_id int, "
        "h_d_id int, h_w_id int, h_amount float);",
        "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, "
        "o_carrier_id int, o_ol_cnt int);",
        "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
        "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, ol_number int, "
        "ol_i_id int, ol_quantity int, ol_amount float, ol_delivery_d int);",
        "create table item (i_id int, i_price float);",
        "create table stock (s_i_id int, s_w_id int, s_quantity int, s_ytd int, s_order_cnt int);",
        "create index warehouse(w_id);",
        "create index district(d_id, d_w_id);",
        "create index customer(c_id, c_d_id, c_w_id);",
        "create index history(h_id);",
        "create index orders(o_id, o_d_id, o_w_id);",
        "create index new_orders(no_o_id, no_d_id, no_w_id);",
        "create index order_line(ol_o_id, ol_d_id, ol_w_id, ol_number);",
        "create index item(i_id);",
        "create index stock(s_i_id, s_w_id);",
    ]
    for sql in statements:
        checked(admin, sql)

    for item_id in range(1, 101):
        checked(admin, f"insert into item values({item_id}, {float(item_id)});")
    for w_id in range(1, 6):
        checked(admin, f"insert into warehouse values({w_id}, 0.0);")
        for item_id in range(1, 101):
            checked(admin, f"insert into stock values({item_id}, {w_id}, 100, 0, 0);")
        for d_id in range(1, 11):
            checked(
                admin,
                f"insert into district values({d_id}, {w_id}, 0.0, {INITIAL_ORDERS_PER_DISTRICT + 1});",
            )
            for c_id in range(1, 21):
                checked(admin, f"insert into customer values({c_id}, {d_id}, {w_id}, 0.0, 0.0, 0, 0);")
            for order_id in range(1, INITIAL_ORDERS_PER_DISTRICT + 1):
                c_id = (order_id - 1) % 20 + 1
                checked(admin, f"insert into orders values({order_id}, {d_id}, {w_id}, {c_id}, 0, 2);")
                if order_id >= INITIAL_NEW_ORDER_START:
                    checked(admin, f"insert into new_orders values({order_id}, {d_id}, {w_id});")
                checked(admin, f"insert into order_line values({order_id}, {d_id}, {w_id}, 1, 1, 1, 1.0, 0);")
                checked(admin, f"insert into order_line values({order_id}, {d_id}, {w_id}, 2, 2, 1, 2.0, 0);")


def begin_snapshot(session):
    checked(session, "set transaction isolation level snapshot isolation;")
    checked(session, "begin;")


def rollback_quietly(session):
    try:
        session.send("rollback;")
    except OSError:
        pass


def run_new_order(session, seq, inject_abort=False):
    w_id = seq % 5 + 1
    d_id = (seq // 5) % 10 + 1
    c_id = (seq // 50) % 20 + 1
    item1 = (seq * 7) % 100 + 1
    item2 = (seq * 13 + 17) % 100 + 1
    if item2 == item1:
        item2 = item2 % 100 + 1
    try:
        begin_snapshot(session)
        order_id = scalar_int(
            session,
            f"select d_next_o_id from district where d_id = {d_id} and d_w_id = {w_id};",
        )
        statements = [
            f"update district set d_next_o_id = {order_id + 1} where d_id = {d_id} and d_w_id = {w_id};",
            f"insert into orders values({order_id}, {d_id}, {w_id}, {c_id}, 0, 2);",
            f"insert into new_orders values({order_id}, {d_id}, {w_id});",
            f"update stock set s_quantity = s_quantity - 1, s_ytd = s_ytd + 1, "
            f"s_order_cnt = s_order_cnt + 1 where s_i_id = {item1} and s_w_id = {w_id};",
            f"insert into order_line values({order_id}, {d_id}, {w_id}, 1, {item1}, 1, {float(item1)}, 0);",
            f"update stock set s_quantity = s_quantity - 1, s_ytd = s_ytd + 1, "
            f"s_order_cnt = s_order_cnt + 1 where s_i_id = {item2} and s_w_id = {w_id};",
            f"insert into order_line values({order_id}, {d_id}, {w_id}, 2, {item2}, 1, {float(item2)}, 0);",
        ]
        for sql in statements:
            response = session.send(sql)
            if failed(response):
                rollback_quietly(session)
                return None
        if inject_abort:
            response = session.send(
                f"insert into order_line values({order_id}, {d_id}, {w_id}, 1, {item1}, 1, 1.0, 0);"
            )
            if not failed(response):
                raise AssertionError("injected duplicate order_line did not abort")
            return None
        response = session.send("commit;")
        if failed(response):
            return None
        return (w_id, d_id, order_id, item1, item2)
    except (AssertionError, RuntimeError, OSError):
        rollback_quietly(session)
        raise


def run_payment(session, worker_id, seq):
    w_id = seq % 5 + 1
    d_id = (seq // 5) % 10 + 1
    c_id = (seq // 50) % 20 + 1
    history_id = worker_id * 1_000_000 + seq
    try:
        begin_snapshot(session)
        statements = [
            f"update warehouse set w_ytd = w_ytd + 1.0 where w_id = {w_id};",
            f"update district set d_ytd = d_ytd + 1.0 where d_id = {d_id} and d_w_id = {w_id};",
            f"update customer set c_balance = c_balance - 1.0, c_ytd_payment = c_ytd_payment + 1.0, "
            f"c_payment_cnt = c_payment_cnt + 1 where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
            f"insert into history values({history_id}, {c_id}, {d_id}, {w_id}, {d_id}, {w_id}, 1.0);",
        ]
        for sql in statements:
            if failed(session.send(sql)):
                rollback_quietly(session)
                return None
        if failed(session.send("commit;")):
            return None
        return (w_id, d_id, c_id)
    except (AssertionError, RuntimeError, OSError):
        rollback_quietly(session)
        raise


def run_delivery(session, seq):
    w_id = seq % 5 + 1
    delivered = []
    try:
        begin_snapshot(session)
        for d_id in range(1, 11):
            order_id = scalar_optional_int(
                session,
                f"select min(no_o_id) from new_orders where no_d_id = {d_id} and no_w_id = {w_id};",
            )
            if order_id is None:
                continue
            c_id = scalar_int(
                session,
                f"select o_c_id from orders where o_id = {order_id} and o_d_id = {d_id} and o_w_id = {w_id};",
            )
            amount = scalar_float(
                session,
                f"select sum(ol_amount) from order_line where ol_o_id = {order_id} and ol_d_id = {d_id} and ol_w_id = {w_id};",
            )
            statements = [
                f"delete from new_orders where no_o_id = {order_id} and no_d_id = {d_id} and no_w_id = {w_id};",
                f"update orders set o_carrier_id = 7 where o_id = {order_id} and o_d_id = {d_id} and o_w_id = {w_id};",
                f"update order_line set ol_delivery_d = 1 where ol_o_id = {order_id} and ol_d_id = {d_id} and ol_w_id = {w_id};",
                f"update customer set c_balance = c_balance + {amount}, "
                f"c_delivery_cnt = c_delivery_cnt + 1 where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
            ]
            for sql in statements:
                if failed(session.send(sql)):
                    rollback_quietly(session)
                    return None
            delivered.append((w_id, d_id, order_id, c_id, amount))
        if failed(session.send("commit;")):
            return None
        return delivered
    except (AssertionError, RuntimeError, OSError):
        rollback_quietly(session)
        raise


def run_order_status(session, seq):
    w_id = seq % 5 + 1
    d_id = (seq // 5) % 10 + 1
    try:
        begin_snapshot(session)
        order_id = scalar_int(
            session,
            f"select max(o_id) from orders where o_w_id = {w_id} and o_d_id = {d_id};",
        )
        checked(session, f"select o_c_id from orders where o_w_id = {w_id} and o_d_id = {d_id} and o_id = {order_id};")
        checked(session, f"select ol_i_id from order_line where ol_w_id = {w_id} and ol_d_id = {d_id} and ol_o_id = {order_id};")
        checked(session, "commit;")
    except (AssertionError, RuntimeError, OSError):
        rollback_quietly(session)
        raise


def run_stock_level(session, seq):
    w_id = seq % 5 + 1
    d_id = (seq // 5) % 10 + 1
    try:
        begin_snapshot(session)
        next_order = scalar_int(
            session,
            f"select d_next_o_id from district where d_id = {d_id} and d_w_id = {w_id};",
        )
        checked(
            session,
            f"select ol_i_id from order_line where ol_w_id = {w_id} and ol_d_id = {d_id} "
            f"and ol_o_id < {next_order} and ol_o_id >= {max(1, next_order - 20)};",
        )
        checked(session, f"select count(*) from stock where s_w_id = {w_id} and s_quantity < 20;")
        checked(session, "commit;")
    except (AssertionError, RuntimeError, OSError):
        rollback_quietly(session)
        raise


def run_workload(port, workers, txns_per_worker):
    committed_new_orders = []
    committed_payments = []
    committed_deliveries = []
    errors = []
    result_lock = threading.Lock()
    start = threading.Event()

    def worker(worker_id):
        session = Session(port)
        local_new = []
        local_payments = []
        local_deliveries = []
        try:
            start.wait()
            for i in range(txns_per_worker):
                seq = worker_id * 10007 + i + 1
                choice = (worker_id * 17 + i * 31) % 100
                if choice < 45:
                    result = run_new_order(session, seq, inject_abort=(seq % 97 == 0))
                    if result is not None:
                        local_new.append(result)
                elif choice < 88:
                    result = run_payment(session, worker_id, seq)
                    if result is not None:
                        local_payments.append(result)
                elif choice < 92:
                    run_order_status(session, seq)
                elif choice < 96:
                    result = run_delivery(session, seq)
                    if result is not None:
                        local_deliveries.extend(result)
                else:
                    run_stock_level(session, seq)
        except Exception as exc:  # noqa: BLE001 - preserve worker diagnostics
            with result_lock:
                errors.append(f"worker {worker_id}: {exc}")
        finally:
            session.close()
        with result_lock:
            committed_new_orders.extend(local_new)
            committed_payments.extend(local_payments)
            committed_deliveries.extend(local_deliveries)

    threads = [threading.Thread(target=worker, args=(worker_id,)) for worker_id in range(workers)]
    for thread in threads:
        thread.start()
    started = time.perf_counter()
    start.set()
    for thread in threads:
        thread.join()
    elapsed = time.perf_counter() - started
    if errors:
        raise AssertionError("\n".join(errors))
    return committed_new_orders, committed_payments, committed_deliveries, elapsed


def prepare_recovery_rid_reuse(admin, port):
    checked(admin, "create table recovery_rid_reuse (id int, v int);")
    checked(admin, "create index recovery_rid_reuse(id);")
    loser = Session(port)
    try:
        checked(loser, "begin;")
        checked(loser, "insert into recovery_rid_reuse values(1, 10);")
        checked(admin, "create static_checkpoint;")
        loser.send("rollback;")
        checked(admin, "insert into recovery_rid_reuse values(2, 20);")
    finally:
        loser.close()


def validate_recovery_rid_reuse(admin, label):
    old_rows = scalar_int(admin, "select count(*) from recovery_rid_reuse where id = 1;")
    new_rows = scalar_int(admin, "select count(*) from recovery_rid_reuse where id = 2 and v = 20;")
    total_rows = scalar_int(admin, "select count(*) from recovery_rid_reuse;")
    if (old_rows, new_rows, total_rows) != (0, 1, 1):
        raise AssertionError(
            f"{label}: checkpoint/rollback RID reuse expected (0, 1, 1), "
            f"got {(old_rows, new_rows, total_rows)}"
        )


def validate(admin, new_orders, payments, deliveries, label):
    initial_orders = 5 * 10 * INITIAL_ORDERS_PER_DISTRICT
    actual_orders = scalar_int(admin, "select count(*) from orders;")
    if actual_orders != initial_orders + len(new_orders):
        raise AssertionError(
            f"{label}: global orders expected {initial_orders + len(new_orders)}, got {actual_orders}"
        )

    stock_rows = scalar_int(admin, "select count(*) from stock;")
    if stock_rows != 500:
        raise AssertionError(f"{label}: stock expected 500 rows, got {stock_rows}")

    expected_stock = {}
    for w_id, _d_id, _order_id, item1, item2 in new_orders:
        for item_id in (item1, item2):
            expected_stock[(w_id, item_id)] = expected_stock.get((w_id, item_id), 0) + 1

    expected_payment_by_w = {}
    expected_payment_by_d = {}
    expected_customer_payment = {}
    for w_id, d_id, c_id in payments:
        expected_payment_by_w[w_id] = expected_payment_by_w.get(w_id, 0) + 1
        expected_payment_by_d[(w_id, d_id)] = expected_payment_by_d.get((w_id, d_id), 0) + 1
        key = (w_id, d_id, c_id)
        expected_customer_payment[key] = expected_customer_payment.get(key, 0) + 1

    expected_customer_delivery = {}
    for w_id, d_id, _order_id, c_id, amount in deliveries:
        key = (w_id, d_id, c_id)
        old_amount, old_count = expected_customer_delivery.get(key, (0.0, 0))
        expected_customer_delivery[key] = (old_amount + amount, old_count + 1)

    for w_id in range(1, 6):
        warehouse_ytd = scalar_float(admin, f"select w_ytd from warehouse where w_id = {w_id};")
        if warehouse_ytd != expected_payment_by_w.get(w_id, 0):
            raise AssertionError(f"{label}: warehouse {w_id} ytd={warehouse_ytd}")
        for d_id in range(1, 11):
            next_order = scalar_int(
                admin,
                f"select d_next_o_id from district where d_w_id = {w_id} and d_id = {d_id};",
            )
            max_order = scalar_int(
                admin,
                f"select max(o_id) from orders where o_w_id = {w_id} and o_d_id = {d_id};",
            )
            max_new = scalar_int(
                admin,
                f"select max(no_o_id) from new_orders where no_w_id = {w_id} and no_d_id = {d_id};",
            )
            min_new = scalar_int(
                admin,
                f"select min(no_o_id) from new_orders where no_w_id = {w_id} and no_d_id = {d_id};",
            )
            count_new = scalar_int(
                admin,
                f"select count(no_o_id) from new_orders where no_w_id = {w_id} and no_d_id = {d_id};",
            )
            sum_lines = scalar_int(
                admin,
                f"select sum(o_ol_cnt) from orders where o_w_id = {w_id} and o_d_id = {d_id};",
            )
            count_lines = scalar_int(
                admin,
                f"select count(ol_o_id) from order_line where ol_w_id = {w_id} and ol_d_id = {d_id};",
            )
            if next_order - 1 != max_order or max_order != max_new:
                raise AssertionError(
                    f"{label}: district/order invariant w={w_id} d={d_id}: "
                    f"next-1={next_order - 1}, max_order={max_order}, max_new={max_new}"
                )
            if count_new != max_new - min_new + 1:
                raise AssertionError(
                    f"{label}: new_orders continuity w={w_id} d={d_id}: "
                    f"count={count_new}, min={min_new}, max={max_new}"
                )
            if sum_lines != count_lines:
                raise AssertionError(
                    f"{label}: order_line count w={w_id} d={d_id}: sum={sum_lines}, count={count_lines}"
                )
            district_ytd = scalar_float(
                admin,
                f"select d_ytd from district where d_w_id = {w_id} and d_id = {d_id};",
            )
            if district_ytd != expected_payment_by_d.get((w_id, d_id), 0):
                raise AssertionError(f"{label}: district ytd w={w_id} d={d_id}: {district_ytd}")

    for w_id in range(1, 6):
        for item_id in range(1, 101):
            updates = expected_stock.get((w_id, item_id), 0)
            quantity = scalar_int(
                admin,
                f"select s_quantity from stock where s_i_id = {item_id} and s_w_id = {w_id};",
            )
            ytd = scalar_int(
                admin,
                f"select s_ytd from stock where s_i_id = {item_id} and s_w_id = {w_id};",
            )
            count = scalar_int(
                admin,
                f"select s_order_cnt from stock where s_i_id = {item_id} and s_w_id = {w_id};",
            )
            if (quantity, ytd, count) != (100 - updates, updates, updates):
                raise AssertionError(
                    f"{label}: stock w={w_id} item={item_id}: "
                    f"expected {(100 - updates, updates, updates)}, got {(quantity, ytd, count)}"
                )

    history_count = scalar_int(admin, "select count(*) from history;")
    if history_count != len(payments):
        raise AssertionError(f"{label}: history expected {len(payments)}, got {history_count}")
    for w_id in range(1, 6):
        for d_id in range(1, 11):
            for c_id in range(1, 21):
                key = (w_id, d_id, c_id)
                payment_count = expected_customer_payment.get(key, 0)
                delivery_amount, delivery_count = expected_customer_delivery.get(key, (0.0, 0))
                balance = scalar_float(
                    admin,
                    f"select c_balance from customer where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
                )
                ytd = scalar_float(
                    admin,
                    f"select c_ytd_payment from customer where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
                )
                actual_payment_count = scalar_int(
                    admin,
                    f"select c_payment_cnt from customer where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
                )
                actual_delivery_count = scalar_int(
                    admin,
                    f"select c_delivery_cnt from customer where c_id = {c_id} and c_d_id = {d_id} and c_w_id = {w_id};",
                )
                expected_balance = delivery_amount - payment_count
                if (balance, ytd, actual_payment_count, actual_delivery_count) != (
                    expected_balance,
                    float(payment_count),
                    payment_count,
                    delivery_count,
                ):
                    raise AssertionError(
                        f"{label}: customer {key}: expected "
                        f"{(expected_balance, payment_count, payment_count, delivery_count)}, got "
                        f"{(balance, ytd, actual_payment_count, actual_delivery_count)}"
                    )

    for w_id, d_id, order_id, _c_id, _amount in deliveries:
        remaining = scalar_int(
            admin,
            f"select count(*) from new_orders where no_o_id = {order_id} and no_d_id = {d_id} and no_w_id = {w_id};",
        )
        carrier = scalar_int(
            admin,
            f"select o_carrier_id from orders where o_id = {order_id} and o_d_id = {d_id} and o_w_id = {w_id};",
        )
        delivered_lines = scalar_int(
            admin,
            f"select sum(ol_delivery_d) from order_line where ol_o_id = {order_id} and ol_d_id = {d_id} and ol_w_id = {w_id};",
        )
        line_count = scalar_int(
            admin,
            f"select count(*) from order_line where ol_o_id = {order_id} and ol_d_id = {d_id} and ol_w_id = {w_id};",
        )
        if remaining != 0 or carrier != 7 or delivered_lines != line_count:
            raise AssertionError(
                f"{label}: delivery w={w_id} d={d_id} o={order_id}: "
                f"remaining={remaining}, carrier={carrier}, delivered={delivered_lines}/{line_count}"
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "bin", "rmdb"))
    parser.add_argument("--db", default="/tmp/phase3_consistency_stress_db")
    parser.add_argument("--port", type=int, default=18843)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--txns-per-worker", type=int, default=40)
    parser.add_argument("--skip-crash", action="store_true")
    args = parser.parse_args()
    if args.workers != 16:
        raise SystemExit("Phase 3 requires exactly 16 workers")

    log_path = "/tmp/phase3_consistency_stress.server.log"
    try:
        os.remove(log_path)
    except FileNotFoundError:
        pass
    server = Server(os.path.abspath(args.binary), args.db, args.port, log_path)
    admin = None
    try:
        server.start(reset=True)
        admin = Session(args.port)
        setup_schema(admin)
        new_orders, payments, deliveries, elapsed = run_workload(
            args.port, args.workers, args.txns_per_worker
        )
        validate(admin, new_orders, payments, deliveries, "live")
        prepare_recovery_rid_reuse(admin, args.port)
        validate_recovery_rid_reuse(admin, "live")
        validate(admin, new_orders, payments, deliveries, "post-checkpoint live")
        print(
            f"live consistency passed: new_orders={len(new_orders)} payments={len(payments)} "
            f"deliveries={len(deliveries)} elapsed={elapsed:.3f}s"
        )
        if not args.skip_crash:
            admin.sock.close()
            admin = None
            server.crash()
            server.start(reset=False)
            admin = Session(args.port)
            validate(admin, new_orders, payments, deliveries, "recovery")
            validate_recovery_rid_reuse(admin, "recovery")
            print("kill -9 recovery consistency passed")
    finally:
        if admin is not None:
            admin.close()
        server.stop()


if __name__ == "__main__":
    main()
