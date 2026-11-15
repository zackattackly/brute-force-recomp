r"""Brute Force (Xbox) sound exporter: wave banks (.xwb) and sound libraries (.mem) -> .wav

Usage:
    python snd2wav.py <game data folder> <converted data folder> [--lang en|de|es|fr|it|all] [--pcm]

  <game data folder>       the disc's data folder (holds sounds\*.xwb, the music)
  <converted data folder>  the output of convert_all.bat (holds the unpacked archives:
                           <level>\sounds-<level>.xml/.mem and ml-sounds\<lang>\...)

Writes everything to <converted data folder>\sounds_wav\:
  music\<level>\<name>.wav       level music and long ambient loops   (data\sounds\<level>.xwb)
  <lang>\<level>\<name>.wav      dialogue and streamed effects        (ml-sounds\<lang>\<level>-<lang>.tgz)
  mp\<bank>\<name>.wav           mp_common\sounds\*.xwb (empty placeholders on the retail disc:
                                 the multiplayer voices are in the language banks)
  memory\<level>\<name>.wav      effects kept in memory: weapons, hits, UI ... (sounds-<level>.mem)
  _sounds.csv                    every sound cue of every level: name, type, max distance, volume,
                                 and the .wav it plays

Formats (reverse-engineered):
  .xwb  XACT wave bank, version 2 ('WBND'): four regions (offset, length): bank data, entry
        table, entry names, wave data. Bank data: flags, entry count, name[16], entry size (24),
        name size (64), alignment (0x800). Entry: flags, format, play offset, play length,
        loop offset, loop length. Format bits: 0-1 codec (0 PCM, 1 Xbox ADPCM), 2-4 channels,
        5-30 sample rate, 31 16-bit PCM.
  sounds-<level>.xml (from the .xmb): <SOUND id=hash(name) type=sfx|speech|ambient> with
        wave-file, falloff, max-dist, volume; <optimized-wave> per wave: resource-type
        SRT_MEMORY (data in the .mem file) / SRT_SHORT_STREAMING (in the language bank) /
        SRT_LONG_STREAMING (in data\sounds\<level>.xwb), and five slots of offset, size, time,
        format, channels, one per language in the order of <languages> (1 English, 3 German,
        5 Spanish, 4 French, 6 Italian). Offsets are file offsets. The format number is the
        same packing as the wave bank's.
  .mem  the SRT_MEMORY waves back to back.
  Xbox ADPCM is IMA ADPCM with 36-byte blocks per channel, so the waves are written unchanged as
  IMA ADPCM .wav files (format 0x11, 65 samples per block), which Windows, VLC and Audacity play.
  --pcm decodes them to plain 16-bit PCM instead (slower; for tools that only read PCM).
Pure Python standard library.
"""
import csv, glob, json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bfhash import bfhash

LANG_IDS = {1: 'en', 3: 'de', 5: 'es', 4: 'fr', 6: 'it', 2: 'ja'}
SOUND_TYPES = {'#04933234': 'sfx', '#012ed199': 'speech'}

# ---------------------------------------------------------------- wave writing
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
        80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
        544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
        2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]
# per (step index, nibble): change of the predictor and new step index, precomputed
_DIFF = [[((s >> 3) + (s if n & 4 else 0) + (s >> 1 if n & 2 else 0) + (s >> 2 if n & 1 else 0)) * (-1 if n & 8 else 1)
          for n in range(16)] for s in STEP]
_NEXT = [[min(88, max(0, i + IDX[n & 7])) for n in range(16)] for i in range(89)]


def unpack_format(f):
    """Wave bank / sound library format number -> (codec, channels, rate, bits)."""
    return f & 3, (f >> 2) & 7, (f >> 5) & 0x3FFFFFF, 16 if f >> 31 else 8


def adpcm_to_pcm(data, ch):
    """Decode Xbox (IMA) ADPCM, 36 bytes per channel per block, to interleaved 16-bit PCM bytes."""
    out = bytearray()
    blk = 36 * ch
    for b in range(0, len(data) - blk + 1, blk):
        chans = []
        for c in range(ch):
            pred, idx = struct.unpack_from('<hB', data, b + 4 * c)
            idx = min(idx, 88)
            smp = [pred]
            for w in range(8):  # 8 words of 4 bytes (8 nibbles) per channel, interleaved
                o = b + 4 * ch + (w * ch + c) * 4
                for byte in data[o:o + 4]:
                    for n in (byte & 15, byte >> 4):
                        pred += _DIFF[idx][n]
                        pred = -32768 if pred < -32768 else 32767 if pred > 32767 else pred
                        idx = _NEXT[idx][n]
                        smp.append(pred)
            chans.append(smp)
        if ch == 1: out += struct.pack('<65h', *chans[0])
        else: out += struct.pack('<%dh' % (65 * ch), *[s for fr in zip(*chans) for s in fr])
    return bytes(out)


def write_wav(path, data, fmt, pcm=False):
    codec, ch, rate, bits = unpack_format(fmt)
    ch = ch or 1
    if codec == 1:
        blk = 36 * ch
        data = data[:len(data) // blk * blk]
        frames = len(data) // blk * 65
        if pcm:
            data = adpcm_to_pcm(data, ch)
            hdr = struct.pack('<HHIIHH', 1, ch, rate, rate * 2 * ch, 2 * ch, 16)
            body = b'fmt ' + struct.pack('<I', len(hdr)) + hdr
        else:
            hdr = struct.pack('<HHIIHHHH', 0x11, ch, rate, rate * blk // 65, blk, 4, 2, 65)
            body = b'fmt ' + struct.pack('<I', len(hdr)) + hdr + b'fact' + struct.pack('<II', 4, frames)
    elif codec == 0:
        hdr = struct.pack('<HHIIHH', 1, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits)
        body = b'fmt ' + struct.pack('<I', len(hdr)) + hdr
    else:
        raise ValueError('unsupported codec %d' % codec)
    body += b'data' + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(body)) + b'WAVE' + body)


def safe(name):
    return re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', name).strip(' .') or '_'

# ---------------------------------------------------------------- wave banks
def read_xwb(path):
    """-> (bank name, [(name, format, file offset, length)])"""
    b = open(path, 'rb').read()
    if b[:4] != b'WBND': raise ValueError('not a wave bank')
    ver = struct.unpack_from('<I', b, 4)[0]
    if ver != 2: raise ValueError('wave bank version %d (only 2 is known)' % ver)
    regs = [struct.unpack_from('<II', b, 8 + 8 * i) for i in range(4)]
    o = regs[0][0]
    flags, count = struct.unpack_from('<II', b, o)
    bank = b[o + 8:o + 24].split(b'\0')[0].decode('latin1')
    esize, nsize = struct.unpack_from('<II', b, o + 24)
    out = []
    for i in range(count):
        e = struct.unpack_from('<6I', b, regs[1][0] + i * esize)
        name = ''
        if regs[2][1] and nsize:
            name = b[regs[2][0] + i * nsize:regs[2][0] + (i + 1) * nsize].split(b'\0')[0].decode('latin1')
        out.append((name or '%s_%03d' % (bank, i), e[1], regs[3][0] + e[2], e[3]))
    return bank, out, b


def export_bank(path, dest, pcm, index):
    bank, entries, b = read_xwb(path)
    for name, fmt, off, ln in entries:
        rel = os.path.join(dest, safe(name) + '.wav')
        write_wav(rel, b[off:off + ln], fmt, pcm)
        index[(os.path.normcase(os.path.abspath(path)), off)] = rel
    return len(entries)

# ---------------------------------------------------------------- sound libraries
def parse_library(path):
    t = open(path, encoding='iso-8859-1').read()
    langs = [int(x) for x in re.findall(r'<(?:language|_1c115b4f)>(\d+)</', t)] or [1]
    sounds = []
    for m in re.finditer(r'<SOUND\b([^>]*)>(.*?)</SOUND>', t, re.S):
        at = dict(re.findall(r'([\w-]+)="([^"]*)"', m.group(1)))
        body = m.group(2)
        def g(tag):
            r = re.search(r'<%s>([^<]*)</%s>' % (tag, tag), body)
            return r.group(1) if r else ''
        sounds.append(dict(id=at.get('id', ''), type=SOUND_TYPES.get(at.get('type'), at.get('type', '')),
                           wave=g('wave-file'), falloff=g('falloff'),
                           max_dist=g('max-dist') or g('_fd40e332'), volume=g('volume')))
    waves = {}
    for w in re.findall(r'<optimized-wave>(.*?)</optimized-wave>', t, re.S):
        fn = re.search(r'<file-name>([^<]*)<', w).group(1)
        rt = re.search(r'<resource-type>([^<]*)<', w).group(1)
        num = lambda tag: [float(x) if '.' in x else int(x) for x in re.findall(r'<%s>([^<]*)</%s>' % (tag, tag), w)]
        waves[fn] = dict(kind=rt, offset=num('offset'), size=num('size'), format=num('format'), time=num('time'))
    return langs, sounds, waves


def load_names():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'names.json')
    try: return {int(k, 16): v for k, v in json.load(open(p)).items()}
    except Exception: return {}


def main(argv):
    pcm = '--pcm' in argv
    lang = 'en'
    if '--lang' in argv: lang = argv[argv.index('--lang') + 1].lower()
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] != '--lang')]
    if len(args) != 2:
        print(__doc__); return 1
    game, conv = args
    out = os.path.join(conv, 'sounds_wav')
    names = load_names()
    index = {}       # (bank file, file offset) -> wav path
    n = 0

    # 1. music: data\sounds\<level>.xwb on the disc
    for p in sorted(glob.glob(os.path.join(game, 'sounds', '*.xwb'))):
        try:
            k = export_bank(p, os.path.join(out, 'music', os.path.basename(p)[:-4]), pcm, index); n += k
            if k: print('music  %-16s %4d waves' % (os.path.basename(p), k), flush=True)
        except Exception as e: print('music  %s: %s' % (os.path.basename(p), e))
    # 2. language banks: ml-sounds\<lang>\<level>-<lang>\<level>-<lang>.xwb (as unpacked by convert_all)
    langs = sorted(os.listdir(os.path.join(conv, 'ml-sounds'))) if os.path.isdir(os.path.join(conv, 'ml-sounds')) else []
    for lg in langs:
        if lang != 'all' and lg != lang: continue
        for p in sorted(glob.glob(os.path.join(conv, 'ml-sounds', lg, '**', '*.xwb'), recursive=True)):
            lvl = os.path.basename(p)[:-4]
            lvl = lvl[:-len(lg) - 1] if lvl.endswith('-' + lg) else lvl
            try:
                k = export_bank(p, os.path.join(out, lg, lvl), pcm, index); n += k
                if k: print('%-6s %-16s %4d waves' % (lg, os.path.basename(p), k), flush=True)
            except Exception as e: print('%s %s: %s' % (lg, os.path.basename(p), e))
    # 3. multiplayer banks inside mp_common
    for p in sorted(glob.glob(os.path.join(conv, 'mp_common', '**', '*.xwb'), recursive=True)):
        try:
            k = export_bank(p, os.path.join(out, 'mp', os.path.basename(p)[:-4]), pcm, index); n += k
            if k: print('mp     %-16s %4d waves' % (os.path.basename(p), k), flush=True)
        except Exception as e: print('mp %s: %s' % (os.path.basename(p), e))

    # 4. sound libraries: memory waves + the cue list
    rows = []
    libs = sorted(glob.glob(os.path.join(conv, '**', 'sounds-*.xml'), recursive=True))
    for lp in libs:
        if os.sep + 'sounds_wav' + os.sep in lp: continue
        lvl = os.path.basename(lp)[len('sounds-'):-4]
        folder = os.path.dirname(lp)
        langs_l, sounds, waves = parse_library(lp)
        slot = 0
        if lang != 'all':
            ids = [LANG_IDS.get(x) for x in langs_l]
            slot = ids.index(lang) if lang in ids else 0
        # which name to give each wave: the first known name of a cue that plays it
        wname = {}
        for s in sounds:
            nm = s['id'] if not s['id'].startswith('#') else names.get(int(s['id'][1:], 16))
            s['name'] = nm or s['id']
            if nm and s['wave'] not in wname: wname[s['wave']] = nm
        mem = os.path.join(folder, 'sounds-%s.mem' % lvl)
        memb = open(mem, 'rb').read() if os.path.exists(mem) else b''
        # the banks this library streams from
        music = os.path.normcase(os.path.abspath(os.path.join(game, 'sounds', lvl + '.xwb')))
        speech = [os.path.normcase(os.path.abspath(p)) for p in
                  glob.glob(os.path.join(conv, 'ml-sounds', lang if lang != 'all' else 'en', '**', '%s-*.xwb' % lvl), recursive=True)]
        speech += [os.path.normcase(os.path.abspath(p)) for p in
                   glob.glob(os.path.join(conv, 'mp_common', '**', '%s.xwb' % lvl), recursive=True)]
        wavpath = {}
        k = 0
        for fn, w in waves.items():
            if w['kind'] == 'SRT_MEMORY':
                if not memb or not w['size'] or not w['size'][0]: continue
                off, size, fmt = w['offset'][0], w['size'][0], w['format'][0]
                p = os.path.join(out, 'memory', lvl, safe(wname.get(fn) or 'wave_' + fn.lstrip('#')) + '.wav')
                write_wav(p, memb[off:off + size], fmt, pcm); k += 1
                wavpath[fn] = p
            else:
                s = slot if w['kind'] == 'SRT_SHORT_STREAMING' and len(w['offset']) > slot and w['size'][slot] else 0
                banks = [music] if w['kind'] == 'SRT_LONG_STREAMING' else speech + [music]
                for bk in banks:
                    if (bk, w['offset'][s]) in index: wavpath[fn] = index[(bk, w['offset'][s])]; break
        n += k
        print('memory %-16s %4d waves, %d cues' % ('sounds-%s' % lvl, k, len(sounds)), flush=True)
        for s in sounds:
            p = wavpath.get(s['wave'], '')
            if s['name'].startswith('#') and p:   # a cue's id is the hash of its wave's name
                stem = os.path.basename(p)[:-4]
                if '#%08x' % bfhash(stem) == s['name']: s['name'] = stem
            rows.append([lvl, s['name'], s['id'], s['type'], s['falloff'], s['max_dist'], s['volume'],
                         waves.get(s['wave'], {}).get('kind', ''), os.path.relpath(p, out) if p else ''])
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, '_sounds.csv'), 'w', newline='', encoding='utf-8') as f:
        wr = csv.writer(f)
        wr.writerow(['level', 'sound', 'id', 'type', 'falloff', 'max-dist', 'volume', 'storage', 'wav'])
        wr.writerows(rows)
    print('%d .wav files and %d sound cues written to %s' % (n, len(rows), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
