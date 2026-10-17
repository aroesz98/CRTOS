"""The system fonts (libgfx; Settings > Appearance chooses the family): free TrueType families
converted at the sizes of the interface scales into rootfs/share/fonts, which goes to the card
as /sd/crtos/share/fonts:

    <family>-regular-<px>.fnt   text
    <family>-bold-<px>.fnt      bold text and headings
    mono-<px>.fnt               monospaced (DejaVu Sans Mono, for every family)
    families.txt                "<id> <name>" a line, the default first
    LICENSE-*.txt               the fonts' licences

The output is kept in the tree; run this again only to change the set:

    python tools/fonts.py [--preview PREVIEW.png]

The sources are downloaded once (pinned by SHA-256) into ~/.crtos/fonts. The sizes must match
the tables of libgfx (system/lib/libgfx/src/settings.c).
"""
import argparse
import hashlib
import io
import os
import sys
import tarfile
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fontconv  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'rootfs', 'share', 'fonts')
CACHE = os.path.join(os.path.expanduser('~'), '.crtos', 'fonts')

# interface scale (percent) -> pixel sizes: text, headings, monospaced
SCALES = (100, 125, 150, 175, 200)
TEXT_PX = (11, 14, 16, 19, 22)
HEADING_PX = (15, 19, 22, 26, 30)
MONO_PX = (10, 12, 15, 17, 20)

DOWNLOADS = {
    'dejavu-fonts-ttf-2.37.tar.bz2': (
        'https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2',
        'fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7'),
    'liberation-fonts-ttf-2.1.5.tar.gz': (
        'https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz',
        '7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0'),
    'NotoSans-Regular.ttf': (
        'https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf',
        '478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823'),
    'NotoSans-Bold.ttf': (
        'https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Bold.ttf',
        '1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913'),
    'NotoSans-OFL.txt': (
        'https://raw.githubusercontent.com/notofonts/latin-greek-cyrillic/main/OFL.txt',
        'cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a'),
}

# (archive or file, member in the archive)
DEJAVU = 'dejavu-fonts-ttf-2.37.tar.bz2'
LIBERATION = 'liberation-fonts-ttf-2.1.5.tar.gz'
FAMILIES = [
    ('dejavu', 'DejaVu Sans', (DEJAVU, 'dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf'),
     (DEJAVU, 'dejavu-fonts-ttf-2.37/ttf/DejaVuSans-Bold.ttf')),
    ('noto', 'Noto Sans', ('NotoSans-Regular.ttf', None), ('NotoSans-Bold.ttf', None)),
    ('liberation', 'Liberation Sans', (LIBERATION, 'liberation-fonts-ttf-2.1.5/LiberationSans-Regular.ttf'),
     (LIBERATION, 'liberation-fonts-ttf-2.1.5/LiberationSans-Bold.ttf')),
    ('dejavu-serif', 'DejaVu Serif', (DEJAVU, 'dejavu-fonts-ttf-2.37/ttf/DejaVuSerif.ttf'),
     (DEJAVU, 'dejavu-fonts-ttf-2.37/ttf/DejaVuSerif-Bold.ttf')),
]
MONO = (DEJAVU, 'dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf')
LICENCES = [
    ('LICENSE-DejaVu.txt', (DEJAVU, 'dejavu-fonts-ttf-2.37/LICENSE')),
    ('LICENSE-Liberation.txt', (LIBERATION, 'liberation-fonts-ttf-2.1.5/LICENSE')),
    ('LICENSE-Noto.txt', ('NotoSans-OFL.txt', None)),
]


def fetch(name):
    """A source file in the cache (downloaded and checked the first time)"""
    url, sha = DOWNLOADS[name]
    path = os.path.join(CACHE, name)
    if os.path.exists(path) and hashlib.sha256(open(path, 'rb').read()).hexdigest() == sha:
        return path
    os.makedirs(CACHE, exist_ok=True)
    print('fonts: downloading %s' % url)
    data = urllib.request.urlopen(url, timeout=120).read()
    got = hashlib.sha256(data).hexdigest()
    if got != sha:
        raise SystemExit('fonts: %s: SHA-256 %s, expected %s' % (name, got, sha))
    with open(path, 'wb') as f:
        f.write(data)
    return path


def source(spec):
    """The bytes of (file, member) - a member of an archive, or the file itself"""
    name, member = spec
    path = fetch(name)
    if member is None:
        return open(path, 'rb').read()
    with tarfile.open(path) as t:
        return t.extractfile(member).read()


def convert(spec, px, out):
    data = source(spec)
    tmp = os.path.join(CACHE, '_font.ttf')
    with open(tmp, 'wb') as f:
        f.write(data)
    glyphs, _, bitmaps, line, _, _ = fontconv.convert(tmp, px)
    fontconv.write_fnt(out, glyphs, bitmaps, 32, 126, line)
    return len(bitmaps) + 8 * len(glyphs) + 16


def preview(path):
    """Every family at every scale in one picture (a look before keeping the set)"""
    from PIL import Image, ImageDraw, ImageFont
    rows = []
    for fid, name, regular, bold in FAMILIES:
        for px, hp in zip(TEXT_PX, HEADING_PX):
            rows.append((regular, px, '%s %d px: Settings  Wallpaper  The quick brown fox 0123' % (name, px)))
        rows.append((bold, HEADING_PX[-1], '%s bold %d px' % (name, HEADING_PX[-1])))
    height = sum(px + 8 for _, px, _ in rows) + 8
    img = Image.new('RGB', (1100, height), (28, 32, 40))
    d = ImageDraw.Draw(img)
    d.fontmode = '1'
    y = 4
    for spec, px, text in rows:
        tmp = os.path.join(CACHE, '_preview.ttf')
        with open(tmp, 'wb') as f:
            f.write(source(spec))
        d.text((8, y), text, font=ImageFont.truetype(tmp, px), fill=(230, 232, 238))
        y += px + 8
    img.save(path)
    print('fonts: preview %s' % path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--preview', metavar='PNG', help='also draw every family at every size into a picture')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        if old.endswith('.fnt'):
            os.remove(os.path.join(OUT, old))
    total = files = 0
    for fid, name, regular, bold in FAMILIES:
        for px in TEXT_PX:
            total += convert(regular, px, os.path.join(OUT, '%s-regular-%d.fnt' % (fid, px)))
            files += 1
        for px in sorted(set(TEXT_PX) | set(HEADING_PX)):
            total += convert(bold, px, os.path.join(OUT, '%s-bold-%d.fnt' % (fid, px)))
            files += 1
    for px in MONO_PX:
        total += convert(MONO, px, os.path.join(OUT, 'mono-%d.fnt' % px))
        files += 1
    with open(os.path.join(OUT, 'families.txt'), 'w', newline='\n') as f:
        f.write('# font families of the interface: <id> <name>, the default first (tools/fonts.py)\n')
        for fid, name, _, _ in FAMILIES:
            f.write('%s %s\n' % (fid, name))
    for out, spec in LICENCES:
        with open(os.path.join(OUT, out), 'wb') as f:
            f.write(source(spec).replace(b'\r\n', b'\n'))
    print('fonts: %d files, %d KB in %s' % (files, total // 1024, OUT))
    if a.preview:
        preview(a.preview)


if __name__ == '__main__':
    main()
