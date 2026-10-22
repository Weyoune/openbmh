#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux native RmlUI integration check; run inside Xvfb with xdotool.

Tests an actual mouse/keyboard inspector edit and shared MCP undo/save. Uses
only generated demo data. No game installation or personal profile is read.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='editor-gui-') as temporary:
        root = Path(temporary)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
        settings = root / 'settings.json'
        settings.write_text(json.dumps({'language': 'de', 'theme': 'midnight', 'mcp_enabled': True, 'mcp_port': port}))
        with (root / 'app.log').open('w') as log:
            app = subprocess.Popen([str(args.executable.resolve()), '--demo', '--page', 'clubs', '--settings', str(settings)],
                                   stdout=log, stderr=log, env={**os.environ, 'LIBGL_ALWAYS_SOFTWARE': '1'})
            try:
                request_id = 0
                def rpc(method, params):
                    nonlocal request_id
                    request_id += 1
                    token = json.loads(settings.read_text()).get('mcp_token', '')
                    request = urllib.request.Request(f'http://127.0.0.1:{port}/mcp',
                        data=json.dumps({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}).encode(),
                        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json',
                                 'Accept': 'application/json, text/event-stream', 'MCP-Protocol-Version': '2025-11-25'})
                    with urllib.request.urlopen(request, timeout=5) as response:
                        value = json.load(response)
                    if 'error' in value:
                        raise AssertionError(value['error'])
                    return value['result']
                deadline = time.monotonic() + 15
                while True:
                    try:
                        rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                           'clientInfo': {'name': 'synthetic-gui-smoke', 'version': '1'}})
                        break
                    except (OSError, ValueError):
                        if app.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('Editor did not start')
                        time.sleep(.05)
                def tool(name, arguments=None):
                    result = rpc('tools/call', {'name': name, 'arguments': arguments or {}})
                    if result.get('isError'):
                        raise AssertionError(result)
                    return result['structuredContent']
                window = subprocess.check_output(['xdotool', 'search', '--sync', '--onlyvisible', '--pid', str(app.pid)],
                                                  text=True, timeout=10).splitlines()[-1]
                def x(*arguments):
                    subprocess.run(['xdotool', *map(str, arguments)], check=True, timeout=5, stdout=subprocess.DEVNULL)
                def click(a, b):
                    x('mousemove', '--window', window, a, b, 'click', 1); time.sleep(.15)
                def wait_name(expected):
                    for _ in range(40):
                        row = tool('editor_get', {'table': 'clubs', 'id': 'club:north'})['record']
                        if row['name']['default'] == expected:
                            return
                        time.sleep(.05)
                    raise AssertionError('Native inspector edit not observed: ' + expected)
                time.sleep(.4); x('windowfocus', window)
                click(1150, 443); x('key', 'ctrl+a'); x('type', '--clearmodifiers', 'FC Native QA')
                click(1300, 741); wait_name('FC Native QA')
                # Exercises textarea values elsewhere in the form as well: the
                # untouched colours must remain real JSON, not escaped markup.
                row = tool('editor_get', {'table': 'clubs', 'id': 'club:north'})['record']
                assert row['colors'] == ['#277c8b', '#ffffff']
                tool('editor_undo'); wait_name('FC Nordstadt')
                time.sleep(.6)
                click(380, 233); x('type', '--clearmodifiers', '--delay', 80, 'Nord')
                time.sleep(.3)
                x('key', 'ctrl+a'); x('key', 'BackSpace'); time.sleep(.3)
                # Native Undo/Redo buttons operate on the same transaction.
                click(1268, 872); wait_name('FC Native QA')
                tool('editor_save', {'path': str(root / 'world.obw')})
                assert (root / 'world.obw').is_file()
                assert tool('editor_info')['dirty'] is False
                print('Native RmlUI edit, JSON fields, search focus, shared undo/redo and OBW save passed.')
            except Exception:
                log.flush()
                print((root / 'app.log').read_text())
                raise
            finally:
                app.kill(); app.wait(timeout=5)


if __name__ == '__main__':
    main()
