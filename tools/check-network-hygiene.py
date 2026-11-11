#!/usr/bin/env python3
"""Privacy and network rules that live in source files, checked on every build.

Run by ctest (tests/CMakeLists.txt). Added in 0.1.86 after the third
outside review (P1, P6):

  1. Every curl that Nexus runs -- in the C++, the image's scripts and
     the Containerfile -- carries --proto =https, so neither the first
     request nor a redirect can fall back to plain http, where anyone on
     the network can rewrite the answer. Text that only shows a person a
     command to type is in SHOWN below.
  2. image/firefox/policies.json is valid JSON and keeps telemetry,
     studies and ad measurement off. Firefox ignores a policies file it
     cannot parse, silently, so a stray comma would turn all of it back
     on with nothing to show for it.
  3. image/network/30-nexus-privacy.conf still says what it is for.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')

# Where shipped code lives.
SOURCES = ['cli', 'system', 'hardware', 'core', 'shop', 'image']
SKIP_PARTS = {'generated', 'artwork', '__pycache__', 'wallpaper'}
SUFFIXES = {'.cpp', '.hpp', '.h', '.sh', '.py', '.qml', '.service', ''}

# curl shown to a person as an example, not run by Nexus: (file, text).
SHOWN = {
    ('system/src/dev.cpp', 'curl -I http://localhost:8080'),
}

CURL = re.compile(r'\bcurl\s+-')

problems = []


def shipped_files():
    for top in SOURCES:
        base = ROOT / top
        if not base.is_dir():
            continue
        for path in base.rglob('*'):
            if not path.is_file() or SKIP_PARTS & set(path.parts):
                continue
            if path.suffix in SUFFIXES or path.name == 'Containerfile':
                yield path


checked = 0
for path in shipped_files():
    try:
        lines = path.read_text(encoding='utf-8').splitlines()
    except (UnicodeDecodeError, OSError):
        continue
    rel = path.relative_to(ROOT).as_posix()
    for number, line in enumerate(lines, 1):
        if not CURL.search(line):
            continue
        stripped = line.strip()
        if stripped.startswith('#') or stripped.startswith('//'):
            continue
        if any(rel == f and text in line for f, text in SHOWN):
            continue
        checked += 1
        if '--proto =https' not in line:
            problems.append(f'{rel}:{number}: curl without --proto =https '
                            f'on the same line:\n    {stripped}')
        elif ' -L' in line or re.search(r'\s-\w*L', line):
            if '--proto-redir =https' not in line:
                problems.append(f'{rel}:{number}: curl follows redirects (-L) '
                                f'without --proto-redir =https:\n    {stripped}')

if checked == 0:
    problems.append('no curl calls found at all -- is the source tree where '
                    'this script was told it is?')

policy_path = ROOT / 'image/firefox/policies.json'
try:
    policies = json.loads(policy_path.read_text(encoding='utf-8'))['policies']
except (OSError, ValueError, KeyError) as error:
    problems.append(f'{policy_path}: not a readable policies file ({error})')
else:
    if policies.get('DisableTelemetry') is not True:
        problems.append(f'{policy_path}: DisableTelemetry is not true')
    if policies.get('DisableFirefoxStudies') is not True:
        problems.append(f'{policy_path}: DisableFirefoxStudies is not true')
    attribution = policies.get('Preferences', {}).get(
        'dom.private-attribution.submission.enabled', {})
    if attribution.get('Value') is not False or attribution.get('Status') != 'locked':
        problems.append(f'{policy_path}: ad measurement '
                        '(dom.private-attribution.submission.enabled) is not locked off')

nm_path = ROOT / 'image/network/30-nexus-privacy.conf'
try:
    nm = nm_path.read_text(encoding='utf-8')
except OSError as error:
    problems.append(f'{nm_path}: {error}')
else:
    for key in ('ipv4.dhcp-send-hostname=false', 'ipv6.dhcp-send-hostname=false',
                'ipv6.ip6-privacy=2'):
        if not re.search(r'^' + re.escape(key) + r'\s*$', nm, re.M):
            problems.append(f'{nm_path}: missing {key}')

if problems:
    print('\n'.join(problems))
    sys.exit(1)
print(f'ok: {checked} curl calls stay on https; Firefox and NetworkManager '
      'privacy files are in order')
