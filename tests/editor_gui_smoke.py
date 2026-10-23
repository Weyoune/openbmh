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
                # Exercise the native comparison, not just its MCP projection:
                # sort, open the first displayed row, edit its first property,
                # and observe which stable record actually changed via MCP.
                def compare_open():
                    click(100, 460); time.sleep(.25)
                def first_property(table, identifier, expected, first_name=False):
                    click(330, 402); time.sleep(.2)
                    click(1150, 443); x('key', 'ctrl+a'); x('type', '--clearmodifiers', expected)
                    click(1300, 741)
                    for _ in range(40):
                        record = tool('editor_get', {'table': table, 'id': identifier})['record']
                        if record['name']['first' if first_name else 'default'] == expected:
                            break
                        time.sleep(.05)
                    else:
                        raise AssertionError('Comparison opened the wrong row: ' + identifier)
                    tool('editor_undo'); time.sleep(.3)
                compare_open()
                click(730, 288)  # Attribute chooser: hide all, select Strength via its label.
                click(495, 405)
                click(900, 461)
                click(1080, 575)
                click(835, 345)  # Sort the remaining rating column descending.
                first_property('players', 'player:demo-14', 'Rating Descending', True)
                compare_open()
                click(978, 345)  # Salary descending: highest known demo wage.
                first_property('players', 'player:demo-50', 'Salary Descending', True)
                compare_open()
                click(978, 345)  # Ascending; missing salary must remain last.
                first_property('players', 'player:demo-0', 'Salary Ascending', True)
                compare_open()
                click(1274, 153)  # Teams.
                click(946, 345)   # Total salary descending.
                first_property('clubs', 'club:south', 'Payroll Descending')
                compare_open()
                click(1378, 153)  # Leagues; its row opens the competition editor.
                first_property('competitions', 'comp:demo', 'League Comparison')
                assert tool('editor_info')['dirty'] is False
                before_rules = tool('editor_info')
                click(100, 509)  # New read-only Regulations page.
                click(475, 232)  # Game rules.
                click(325, 232)  # Leagues & competitions.
                click(650, 233); x('type', '--clearmodifiers', 'League')
                time.sleep(.2); x('key', 'ctrl+a'); x('key', 'BackSpace')
                rules = tool('editor_regulations', {'competition': 'comp:demo', 'language': 'en'})
                assert rules['read_only'] is True and len(rules['items']) == 1
                assert rules['items'][0]['movement_defined'] is False
                assert tool('editor_info') == before_rules
                # Two distinct leagues ensure drilling filters rows rather than
                # merely opening the first club/player from the whole world.
                tool('editor_apply', {'changes': [
                    {'op': 'create', 'table': 'competitions', 'id': 'comp:second',
                     'name': 'Zweite Demo-Liga', 'data': {'nation': 'nation:de', 'tier': 2}},
                    {'op': 'create', 'table': 'competitions', 'id': 'comp:cup',
                     'name': 'Zzz-Testpokal', 'data': {'nation': 'nation:de', 'type': 'cup'}},
                    {'op': 'update', 'table': 'clubs', 'id': 'club:south',
                     'set': {'/competition': 'comp:second'}}]})
                tool('editor_save', {'path': str(root / 'world.obw')})
                time.sleep(.6)
                def double_click(a, b):
                    before = tool('editor_info')
                    x('mousemove', '--window', window, a, b, 'click', '--repeat', 2, '--delay', 120, 1)
                    time.sleep(.4)
                    assert tool('editor_info') == before, 'Navigation must not mutate the world'
                def edit_selected(table, identifier, expected, first=False):
                    click(1150, 443); x('key', 'ctrl+a'); x('type', '--clearmodifiers', expected)
                    click(1300, 741)
                    for _ in range(40):
                        name = tool('editor_get', {'table': table, 'id': identifier})['record']['name']
                        if name['first' if first else 'default'] == expected:
                            break
                        time.sleep(.05)
                    else:
                        raise AssertionError('Double click did not filter the expected destination: ' + identifier)
                    tool('editor_undo'); time.sleep(.5)
                south_player = tool('editor_list', {'table': 'players', 'team': 'club:south',
                                                    'sort': 'name', 'limit': 1})['items'][0]['id']
                click(100, 264)  # Leagues, then the unselected second row.
                double_click(350, 385)
                edit_selected('clubs', 'club:south', 'Filtered South')
                double_click(350, 335)  # That club's players, all nationalities.
                edit_selected('players', south_player, 'Filtered Player', True)
                click(100, 264)  # Root navigation clears hidden team filters.
                double_click(350, 335)
                edit_selected('clubs', 'club:north', 'Filtered North')
                click(100, 313)  # Double click a club that was not already selected.
                double_click(350, 385)
                edit_selected('players', south_player, 'Second Club', True)
                click(100, 313)
                click(1150, 443); x('key', 'ctrl+a'); x('type', '--clearmodifiers', 'Keep Draft')
                double_click(350, 385)
                x('key', 'Escape'); time.sleep(.2)
                click(1300, 741); wait_name('Keep Draft')
                tool('editor_undo'); time.sleep(.5)
                click(100, 215); double_click(350, 335)  # Country -> its competitions.
                double_click(350, 434)  # A cup keeps its own inspector.
                edit_selected('competitions', 'comp:cup', 'Cup Inspector')
                print('Native edit, comparisons, rules, league/club drill-down and draft protection passed.')
            except Exception:
                log.flush()
                print((root / 'app.log').read_text())
                raise
            finally:
                app.kill(); app.wait(timeout=5)


if __name__ == '__main__':
    main()
