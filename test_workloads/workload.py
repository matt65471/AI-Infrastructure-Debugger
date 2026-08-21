#!/usr/bin/env python3

import argparse
import multiprocessing
import socket
import time


CHUNK_SIZE = 1024 * 1024


def run_cpu_worker(end_time: float) -> None:
    value = 0
    while time.monotonic() < end_time:
        value = (value * 3 + 1) % 1_000_000_007


def run_cpu(args: argparse.Namespace) -> None:
    end_time = time.monotonic() + args.duration
    workers = []

    for _ in range(args.workers):
        process = multiprocessing.Process(target=run_cpu_worker, args=(end_time,))
        process.start()
        workers.append(process)

    for process in workers:
        process.join()


def run_memory(args: argparse.Namespace) -> None:
    chunks = []
    for _ in range(args.mb):
        chunks.append(bytearray(CHUNK_SIZE))

    print(f"allocated_mb={args.mb}")
    time.sleep(args.duration)
    print("memory workload complete")


def run_net_server(args: argparse.Namespace) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen()
        print(f"listening_on={args.host}:{args.port}")

        while True:
            connection, address = server.accept()
            with connection:
                print(f"client_connected={address[0]}:{address[1]}")
                while True:
                    data = connection.recv(CHUNK_SIZE)
                    if not data:
                        break


def run_net_client(args: argparse.Namespace) -> None:
    remaining_bytes = args.mb * CHUNK_SIZE
    payload = b"x" * CHUNK_SIZE

    with socket.create_connection((args.host, args.port)) as connection:
        while remaining_bytes > 0:
            bytes_to_send = min(len(payload), remaining_bytes)
            connection.sendall(payload[:bytes_to_send])
            remaining_bytes -= bytes_to_send

    print(f"sent_mb={args.mb}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate simple CPU, memory, and network load."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    cpu_parser = subparsers.add_parser("cpu", help="burn CPU for a duration")
    cpu_parser.add_argument("--workers", type=int, default=1)
    cpu_parser.add_argument("--duration", type=int, default=30)
    cpu_parser.set_defaults(func=run_cpu)

    memory_parser = subparsers.add_parser("memory", help="allocate memory")
    memory_parser.add_argument("--mb", type=int, default=256)
    memory_parser.add_argument("--duration", type=int, default=30)
    memory_parser.set_defaults(func=run_memory)

    server_parser = subparsers.add_parser("net-server", help="receive TCP traffic")
    server_parser.add_argument("--host", default="127.0.0.1")
    server_parser.add_argument("--port", type=int, default=9000)
    server_parser.set_defaults(func=run_net_server)

    client_parser = subparsers.add_parser("net-client", help="send TCP traffic")
    client_parser.add_argument("--host", default="127.0.0.1")
    client_parser.add_argument("--port", type=int, default=9000)
    client_parser.add_argument("--mb", type=int, default=256)
    client_parser.set_defaults(func=run_net_client)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    if hasattr(args, "workers") and args.workers <= 0:
        parser.error("--workers must be greater than 0")
    if hasattr(args, "duration") and args.duration <= 0:
        parser.error("--duration must be greater than 0")
    if hasattr(args, "mb") and args.mb <= 0:
        parser.error("--mb must be greater than 0")

    args.func(args)


if __name__ == "__main__":
    main()
