#!/usr/bin/env python3
"""Fetch the third-party sources of third_party/sources.txt and apply CRTOS' changes (patches/).

    python tools/thirdparty.py               the "base" sources (what every build needs): fetch
                                             what is missing, bring the rest up to date
    python tools/thirdparty.py netsurf       also these groups or directories (--all: every one)
    python tools/thirdparty.py --status      the state of each directory
    python tools/thirdparty.py --save [DIR]  after changing a checkout: write its changes to
                                             patches/<directory>.patch (these go into git)
    python tools/thirdparty.py --build       what CMake runs on every configure: the base
                                             sources as above, and the checkouts of the other
                                             groups that are there are brought up to date;
                                             prints nothing when there is nothing to do

Each directory is a git checkout of one commit (a shallow fetch). patches/<directory>.patch
holds the CRTOS changes of that directory; the checkout keeps a copy of the patch it has
applied (.git/crtos.patch), so a new commit in sources.txt or a new patch is taken over
automatically: the old patch comes off, the new commit is checked out, the new patch goes on.
A checkout with changes of its own (not in its patch) is never touched - --save them first.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SOURCES = os.path.join(ROOT, 'third_party', 'sources.txt')
PATCHES = os.path.join(ROOT, 'patches')
STORED = 'crtos.patch'          # in the checkout's .git: the patch applied now


class Source:
    def __init__(self, line):
        d, self.url, self.commit, *opts = line.split()
        self.dir = d
        self.only, self.skip, self.group = [], [], 'base'
        for o in opts:
            key, _, value = o.partition('=')
            if key == 'only':
                self.only = value.split(',')
            elif key == 'skip':
                self.skip = value.split(',')
            elif key == 'group':
                self.group = value
            else:
                sys.exit('thirdparty: %s: unknown option %s' % (SOURCES, o))

    def patch(self):
        return os.path.join(PATCHES, *self.dir.split('/')) + '.patch'


def sources():
    """The entries of sources.txt (an entry goes on over lines that end with a backslash: joined
    as they are, so "a,\\" + "b" is "a,b" and "x \\" + "y" is "x y")"""
    entry = ''
    with open(SOURCES, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if line.endswith('\\'):
                entry += line[:-1]
                continue
            entry += line
            if entry.strip():
                yield Source(entry.strip())
            entry = ''


def git(args, cwd, check=True, data=None):
    r = subprocess.run(['git'] + args, cwd=cwd, capture_output=True, input=data)
    if check and r.returncode:
        sys.exit('thirdparty: git %s (in %s) failed:\n%s'
                 % (' '.join(args), cwd, r.stderr.decode(errors='replace').strip()))
    return r


def out(r):
    return r.stdout.decode(errors='replace')


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def is_checkout(path):
    return os.path.exists(os.path.join(path, '.git'))


def head(path):
    """The commit checked out (a detached HEAD holds it; anything else asks git)"""
    h = (read(os.path.join(path, '.git', 'HEAD')) or b'').decode().strip()
    if len(h) == 40 and not h.startswith('ref:'):
        return h
    return out(git(['rev-parse', 'HEAD'], path)).strip()


def apply(path, patch, reverse=False, check=False):
    """git apply of the patch's bytes; True when it applies (or with check: would apply)"""
    args = ['apply', '--whitespace=nowarn']
    if reverse:
        args.append('--reverse')
    if check:
        args.append('--check')
    return git(args + ['-'], path, check=False, data=patch).returncode == 0


def stored(path):
    return read(os.path.join(path, '.git', STORED))


def store(path, patch):
    p = os.path.join(path, '.git', STORED)
    if patch is None:
        if os.path.exists(p):
            os.remove(p)
    else:
        with open(p, 'wb') as f:
            f.write(patch)


def changed_lines(diff):
    return [line for line in diff.splitlines() if line[:1] in (b'+', b'-')]


def changed(path):
    return out(git(['status', '--porcelain', '--untracked-files=no'], path)).splitlines()


def fetch_commit(s, path):
    args = ['fetch', '-q', '--depth', '1']
    if s.only:
        args.append('--filter=blob:none')      # only the files of the sparse paths come over
    git(args + ['origin', s.commit], path)
    git(['checkout', '-q', 'FETCH_HEAD'], path)


def sparse(s, path):
    """The sparse checkout of only= and skip= (applied by the next checkout)"""
    if not (s.only or s.skip):
        return
    if s.skip:
        # names like "id:000023,..." cannot exist on Windows: those directories stay out
        git(['config', 'core.protectNTFS', 'false'], path)
    git(['config', 'core.sparseCheckout', 'true'], path)
    git(['config', 'core.sparseCheckoutCone', 'false'], path)
    lines = ['/%s\n' % o for o in s.only] or ['/*\n']
    lines += ['!/%s/\n' % k for k in s.skip]
    with open(os.path.join(path, '.git', 'info', 'sparse-checkout'), 'w', newline='\n') as f:
        f.write(''.join(lines))


def lf_config(path):
    """The files as the repository holds them (LF), whatever the computer's git settings say.
    The patches are diffs of that form, and "git apply" without --index does not see the
    attributes that only the index has: with a sparse checkout that leaves the root
    .gitattributes out (lwIP: "*.c text"), Windows would get CRLF files no LF patch matches."""
    git(['config', 'core.autocrlf', 'false'], path)
    git(['config', 'core.eol', 'lf'], path)
    git(['config', 'crtos.lf', 'true'], path)       # the files are written so


def lf_checkout(path):
    """A checkout made before lf_config: set it and write the files again (only for a
    checkout without changes); False when that was done already"""
    if out(git(['config', '--get', 'crtos.lf'], path, check=False)).strip() == 'true':
        return False
    lf_config(path)
    # checkout-index leaves a file whose stat matches the index alone, even with -f: remove
    # the files first (those on disk - the index of a sparse checkout lists the others too);
    # -u records the new sizes, or git would take every file for changed (the index would
    # keep the CRLF sizes, and a different size counts as a change without a look inside)
    for f in out(git(['ls-files', '-z'], path)).split('\0'):
        p = os.path.join(path, *f.split('/'))
        if f and os.path.isfile(p):
            os.remove(p)
    git(['checkout-index', '-a', '-u'], path)
    return True


def clone(s, path):
    os.makedirs(path, exist_ok=True)
    git(['init', '-q'], path)
    git(['config', 'core.longpaths', 'true'], path)        # Windows: deep paths
    lf_config(path)
    sparse(s, path)
    git(['remote', 'add', 'origin', s.url], path)
    fetch_commit(s, path)


def sync(s, dest, fetch_missing, say):
    """Bring one directory to its commit and patch; False on a problem (told)"""
    path = os.path.join(dest, *s.dir.split('/'))
    want = read(s.patch())
    notes = []
    if not is_checkout(path):
        if not fetch_missing:
            return True
        if os.path.isdir(path) and os.listdir(path):
            say(s.dir, 'exists but is not a git checkout (an old copy?): move it away', True)
            return False
        say(s.dir, 'fetching %s' % s.url)
        clone(s, path)
        notes.append('fetched')
    else:
        applied = stored(path)
        if applied is None and want is not None and apply(path, want, reverse=True, check=True):
            store(path, want)           # applied before the copy was kept
            applied = want
        at = head(path)
        if at == s.commit and applied == want:
            return True
        # something to change: the applied patch must come off cleanly, and nothing else
        # may be changed, or the change would be lost
        if applied is not None and not apply(path, applied, reverse=True, check=True):
            say(s.dir, 'has changes of its own over the CRTOS patch: --save them or discard them', True)
            return False
        if applied is not None:
            apply(path, applied, reverse=True)
        if changed(path):
            if applied is not None:
                apply(path, applied)
            say(s.dir, 'has changes of its own (git status): --save them or discard them', True)
            return False
        store(path, None)
        if at != s.commit:
            sparse(s, path)
            fetch_commit(s, path)
            notes.append('now at %s' % s.commit[:12])
    if want is not None:
        # here the checkout has no changes (fetched now, or checked above)
        if not apply(path, want, check=True) and lf_checkout(path):
            notes.append('files rewritten with LF line ends')
        if not apply(path, want):
            say(s.dir, 'THE PATCH DOES NOT APPLY: %s' % os.path.relpath(s.patch(), ROOT), True)
            return False
        store(path, want)
        notes.append('patch applied')
    elif notes == []:
        notes.append('patch removed')
    say(s.dir, ', '.join(notes))
    return True


def say(d, text, problem=False):
    print('%-28s %s' % (d, text), file=sys.stderr if problem else sys.stdout, flush=True)


def run(dest, wanted, build):
    """Sync the base group and the wanted groups/directories; with build also the checkouts
    of the others that are there"""
    ok = True
    for s in sources():
        selected = s.group == 'base' or s.group in wanted or s.dir in wanted or 'all' in wanted
        if selected or build:
            ok = sync(s, dest, selected, say) and ok
    return ok


def status(dest):
    for s in sources():
        path = os.path.join(dest, *s.dir.split('/'))
        if not is_checkout(path):
            print('%-28s %-8s not fetched' % (s.dir, s.group))
            continue
        at = head(path)
        want, applied = read(s.patch()), stored(path)
        if want is None:
            patch = 'none' if applied is None else 'one is applied that patches/ does not have'
        elif applied == want:
            patch = 'applied'
        else:
            patch = 'not applied' if applied is None else 'an older one applied'
        # own changes: the checkout's changed lines are not just the applied patch's (line
        # numbers and context differ when a patch went onto another commit)
        diff = git(['diff', '--binary', s.commit], path).stdout
        own = 'no' if changed_lines(diff) == changed_lines(applied or b'') else 'yes'
        print('%-28s %-8s %s  patch: %s  own changes: %s' % (
            s.dir, s.group, 'ok' if at == s.commit else 'OTHER COMMIT ' + at[:12], patch, own))
    return True


def save(dest, which):
    ok = True
    for s in sources():
        if which and s.dir not in which:
            continue
        path = os.path.join(dest, *s.dir.split('/'))
        if not is_checkout(path):
            continue
        diff = git(['diff', '--binary', s.commit], path).stdout
        untracked = out(git(['ls-files', '--others', '--exclude-standard'], path)).split()
        p = s.patch()
        if diff:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            if read(p) != diff:
                with open(p, 'wb') as f:
                    f.write(diff)
                print('%-28s %s (%d lines)' % (s.dir, os.path.relpath(p, ROOT), diff.count(b'\n')))
            store(path, diff)
        elif os.path.exists(p):
            print('%-28s no changes: %s is not needed any more' % (s.dir, os.path.relpath(p, ROOT)))
        if untracked:
            print('%-28s new files are not in the patch (git add -N them first): %s'
                  % (s.dir, ' '.join(untracked[:8])))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='groups (netsurf) or directories to fetch besides the base ones')
    ap.add_argument('--all', action='store_true', help='every group')
    ap.add_argument('--status', action='store_true', help='show the state of each directory')
    ap.add_argument('--save', action='store_true', help='write the changes of the named (or all) checkouts to patches/')
    ap.add_argument('--build', action='store_true', help='what the CMake configure runs')
    ap.add_argument('--dest', default=os.path.join(ROOT, 'third_party'), help='instead of third_party/')
    a = ap.parse_args()
    if a.status:
        return 0 if status(a.dest) else 1
    if a.save:
        return 0 if save(a.dest, a.names) else 1
    known = {s.group for s in sources()} | {s.dir for s in sources()}
    for n in a.names:
        if n not in known:
            sys.exit('thirdparty: %s is neither a group nor a directory of %s' % (n, os.path.relpath(SOURCES, ROOT)))
    return 0 if run(a.dest, set(a.names) | ({'all'} if a.all else set()), a.build) else 1


if __name__ == '__main__':
    sys.exit(main())
