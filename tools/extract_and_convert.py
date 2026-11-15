"""Unpack every Brute Force .tgz archive and convert all .xmb files inside to readable .xml.

Usage:  python extract_and_convert.py "<game folder>\\data" "<output folder>"

- .tgz files are ordinary gzip-compressed tar archives (the game unpacks them to the Xbox Z: cache).
- .xmb (binary XML) needs the .xsb schemas from common.tgz, so common is processed first.
- Each archive ends up in its own folder (as the game does on the Z: cache drive);
  each .xml is written next to its .xmb. Names that could not be recovered appear as _xxxxxxxx (hash).
"""
import sys, os, glob, tarfile, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmb2xml as X

def main(data_dir, out_dir):
    tgzs = sorted(glob.glob(os.path.join(data_dir, '**', '*.tgz'), recursive=True))
    tgzs.sort(key=lambda p: (os.path.basename(p) != 'common.tgz', p))
    os.makedirs(out_dir, exist_ok=True)
    print(f'{len(tgzs)} archives found in {data_dir}')
    for i, t in enumerate(tgzs, 1):
        rel = os.path.relpath(os.path.dirname(t), data_dir)
        name = os.path.basename(t)[:-len('.tgz')]
        try:
            with tarfile.open(t, 'r:gz') as tf:
                members = tf.getmembers()
                # The game's archives already hold a top folder named after the archive (mp1.tgz -> mp1/...).
                # Only add a folder of our own when an archive doesn't, so each archive still gets its own folder.
                tops = {m.name.replace('\\', '/').split('/')[0] for m in members}
                dest = os.path.normpath(os.path.join(out_dir, rel) if tops == {name} else os.path.join(out_dir, rel, name))
                for m in members:
                    target = os.path.normpath(os.path.join(dest, m.name))
                    if not target.startswith(os.path.normpath(dest)):  # refuse path traversal
                        continue
                    if m.isdir(): os.makedirs(target, exist_ok=True)
                    elif m.isfile():
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        with tf.extractfile(m) as src, open(target, 'wb') as dst: dst.write(src.read())
            print(f'[{i}/{len(tgzs)}] unpacked {os.path.relpath(t, data_dir)}', flush=True)
        except Exception as e:
            print(f'[{i}/{len(tgzs)}] FAILED to unpack {os.path.relpath(t, data_dir)}: {e}', flush=True)
    schemas = X.Schemas(sorted(glob.glob(os.path.join(out_dir, '**', '*.xsb'), recursive=True)))
    ok = bad = empty = 0
    files = sorted(glob.glob(os.path.join(out_dir, '**', '*.xmb'), recursive=True))
    print(f'converting {len(files)} .xmb files...', flush=True)
    for n, f in enumerate(files, 1):
        if n % 500 == 0: print(f'  {n}/{len(files)}', flush=True)
        if os.path.getsize(f) == 0:
            empty += 1; print(f'empty in the game files, skipped: {os.path.relpath(f, out_dir)}'); continue
        try:
            xml, end, size = X.convert(f, schemas)
            open(f[:-4] + '.xml', 'w', encoding='iso-8859-1', errors='replace').write(xml)
            if end == size: ok += 1
            else: bad += 1; print(f'WARNING {size-end} unread bytes: {f}')
        except Exception:
            bad += 1; print('FAILED', f); traceback.print_exc(limit=1)
    print(f'converted {ok} files exactly, {bad} with problems, {empty} empty placeholder files skipped')

if __name__ == '__main__':
    if len(sys.argv) != 3: print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2])
