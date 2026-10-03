#!/usr/bin/env python3
"""Render the PlantUML diagrams of the design documentation (docs/specyfikacja/diagramy).

    python tools/diagrams.py            render the .puml files whose .svg is missing or older
    python tools/diagrams.py --all      render every diagram
    python tools/diagrams.py --check    only check the syntax (nothing is written)
    python tools/diagrams.py FILE...    these files (or directories)

Each diagram is a .puml source; the .svg next to it is what the Markdown documents show.
Rendering happens locally (no PlantUML server): it needs a Java 11+ runtime - java on the
PATH, JAVA_HOME, or the one that comes with MCUXpresso IDE - and PlantUML, which is
downloaded once into ~/.crtos/tools (or set CRTOS_PLANTUML to a plantuml.jar). The diagrams
use PlantUML's built-in "smetana" layout, so Graphviz is not needed.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import urllib.request

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
DIAGRAMS = os.path.join(ROOT, 'docs', 'specyfikacja', 'diagramy')
STYLE = os.path.join(DIAGRAMS, 'styl.iuml')

PLANTUML_VERSION = '1.2026.8'
PLANTUML_JAR = 'plantuml-mit-%s.jar' % PLANTUML_VERSION      # the MIT licensed build
PLANTUML_URL = 'https://github.com/plantuml/plantuml/releases/download/v%s/%s' % (PLANTUML_VERSION, PLANTUML_JAR)
PLANTUML_SHA256 = '3629c9cd017c7f73e6450396eea0040216c7e1eef8473ce33cc1aad469dab2f9'
STATE = os.environ.get('CRTOS_STATE') or os.path.join(os.path.expanduser('~'), '.crtos')


class Fail(Exception):
    pass


def find_java():
    exe = 'java.exe' if os.name == 'nt' else 'java'
    if os.environ.get('JAVA_HOME'):
        j = os.path.join(os.environ['JAVA_HOME'], 'bin', exe)
        if os.path.exists(j):
            return j
    j = shutil.which('java')
    if j:
        return j
    # the Java runtime of MCUXpresso IDE (Eclipse JustJ)
    for place in ('C:/nxp/MCUXpressoIDE_*/ide/plugins/org.eclipse.justj.openjdk.*/jre/bin/' + exe,
                  '/usr/local/mcuxpressoide*/ide/plugins/org.eclipse.justj.openjdk.*/jre/bin/' + exe,
                  '/Applications/MCUXpressoIDE_*/ide/*.app/Contents/Eclipse/plugins/org.eclipse.justj.openjdk.*/jre/bin/' + exe):
        found = sorted(glob.glob(place), reverse=True)
        if found:
            return found[0]
    raise Fail('no Java runtime: install Java 11 or newer (or set JAVA_HOME)')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def find_plantuml():
    if os.environ.get('CRTOS_PLANTUML'):
        return os.environ['CRTOS_PLANTUML']
    jar = os.path.join(STATE, 'tools', PLANTUML_JAR)
    if os.path.exists(jar) and sha256(jar) == PLANTUML_SHA256:
        return jar
    os.makedirs(os.path.dirname(jar), exist_ok=True)
    print('diagrams: downloading PlantUML %s ...' % PLANTUML_VERSION)
    tmp = jar + '.part'
    urllib.request.urlretrieve(PLANTUML_URL, tmp)
    if sha256(tmp) != PLANTUML_SHA256:
        os.remove(tmp)
        raise Fail('%s: unexpected SHA-256, not used' % PLANTUML_URL)
    os.replace(tmp, jar)
    return jar


def sources(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for d, _, files in os.walk(p):
                out += [os.path.join(d, f) for f in files if f.endswith('.puml')]
        elif p.endswith('.puml'):
            out.append(p)
    return sorted(out)


def stale(puml):
    svg = os.path.splitext(puml)[0] + '.svg'
    if not os.path.exists(svg):
        return True
    newest = max(os.path.getmtime(puml), os.path.getmtime(STYLE) if os.path.exists(STYLE) else 0)
    return os.path.getmtime(svg) < newest


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('paths', nargs='*', help='.puml files or directories (default: docs/specyfikacja/diagramy)')
    ap.add_argument('--all', action='store_true', help='render every diagram, also unchanged ones')
    ap.add_argument('--check', action='store_true', help='only check the syntax')
    a = ap.parse_args()
    files = sources(a.paths or [DIAGRAMS])
    if not a.all and not a.check:
        files = [f for f in files if stale(f)]
    if not files:
        print('diagrams: nothing to do')
        return 0
    cmd = [find_java(), '-Djava.awt.headless=true', '-jar', find_plantuml(), '-charset', 'UTF-8', '-failfast2']
    cmd += ['-checkonly'] if a.check else ['-tsvg']
    r = subprocess.run(cmd + files)
    if r.returncode:
        raise Fail('PlantUML reported errors (exit code %d)' % r.returncode)
    print('diagrams: %d file(s) %s' % (len(files), 'checked' if a.check else 'rendered'))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Fail as e:
        print('diagrams: %s' % e, file=sys.stderr)
        sys.exit(1)
