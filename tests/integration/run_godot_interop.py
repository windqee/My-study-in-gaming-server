#!/usr/bin/env python3
"""Real headless Godot ↔ native ENet interop; bounded waits and in-memory diagnostic capture."""
import argparse
import collections
import pathlib
import queue
import signal
import shutil
import tempfile
import subprocess
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    parser.add_argument('--godot', required=True)
    parser.add_argument('--project', required=True)
    args = parser.parse_args()
    def cancelled(_signum, _frame):
        raise SystemExit(130)
    signal.signal(signal.SIGTERM, cancelled)
    server = None
    lines = collections.deque(maxlen=256)
    lines_lock = threading.Lock()

    def server_lines():
        with lines_lock:
            return list(lines)
    ready = queue.Queue(maxsize=1)
    source_project = pathlib.Path(args.project).resolve()
    temporary_project = tempfile.TemporaryDirectory(prefix="netarena-godot-acceptance-")
    project = str(pathlib.Path(temporary_project.name) / "godot")

    def run(command, marker=None, timeout=15):
        # Files prevent unlimited pipe accumulation and pipe-buffer deadlocks.
        with tempfile.TemporaryFile(mode='w+') as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            try:
                process.wait(timeout=timeout)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
            output.seek(0)
            text = output.read(65536)
            print(text, end='')
            if process.returncode or (marker and marker not in text):
                raise RuntimeError(f'command failed or missing marker {marker}: exit={process.returncode}')

    def read_server():
        for line in server.stdout:
            line = line[:1024].strip()
            with lines_lock:
                lines.append(line)
            if line.startswith('READY port='):
                try:
                    ready.put_nowait(int(line.split('=', 1)[1]))
                except queue.Full:
                    pass

    try:
        # Exercise first-run behavior every time, without touching the user's
        # editor cache. Runtime entrypoints explicitly load the native library;
        # this asset-free project does not need editor import/documentation.
        shutil.copytree(source_project, project, ignore=shutil.ignore_patterns('.godot'))
        server = subprocess.Popen([args.server, '--port', '0', '--max-seconds', '40'],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        reader = threading.Thread(target=read_server, daemon=True)
        reader.start()
        port = ready.get(timeout=8)
        command = [args.godot, '--headless', '--path', project, '--script',
                   'res://scripts/interop_test.gd', '--', '--port', str(port)]
        run(command, 'INTEROP_OK nonce=9007199254740993')
        run(command + ['--bad-version'], 'VERSION_REJECTED')
        until = time.monotonic() + 2
        while time.monotonic() < until and not any('PROTOCOL_MISMATCH' in line for line in server_lines()):
            time.sleep(0.01)
        if not any('PONG' in line for line in server_lines()) or not any('PROTOCOL_MISMATCH' in line for line in server_lines()):
            raise RuntimeError('server did not observe pong and protocol rejection')
        print('GODOT_INTEROP_PASS')
    finally:
        if server is not None:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3)
            if server.stdout:
                reader.join(timeout=2)
                server.stdout.close()
            print('\n'.join(server_lines()))
        temporary_project.cleanup()


if __name__ == '__main__':
    main()
