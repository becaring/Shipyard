"""Static draw-state harvest for the prebuilt shader pack; no game code runs.

Display lists are simulated with the RDP state the interpreter reads (combine,
other modes, tile clamp modes, grayscale): LUS O2R display-list resources,
following OTR calls, and raw F3DEX2 lists found in other archive blobs. State a
list inherits from its caller is unknown and filled from C-side seeds: setup
display lists and constant gDP* state calls, compiled with the gbi.h
macros by a host dump program. The result is a superset of runtime draw states.
"""

import os
import re
import struct
import zipfile
from multiprocessing import Pool
from pathlib import Path

DISPLAY_LIST = 0x4F444C54  # 'ODLT'
UCODE_F3DEX2 = 4
OTR_HEADER = 64
MAX_DEPTH = 32
FULL = (1 << 64) - 1
# F3DEX2 with the LUS OTR extensions; consumers override values that differ.
F3DEX2 = dict(
    TRI1=0x05, TRI2=0x06, QUAD=0x07, TRI1_OTR=0x26, TRI1_WIDE=0x49,
    DL=0xDE, ENDDL=0xDF, TEXTURE=0xD7,
    SETOTHERMODE_H=0xE3, SETOTHERMODE_L=0xE2, RDPSETOTHERMODE=0xEF,
    SETCOMBINE=0xFC, SETTILE=0xF5, FILLRECT=0xF6,
    TEXRECT=0xE4, TEXRECTFLIP=0xE5, TEXRECT_WIDE=0x37, IMAGERECT=0x3C,
    DL_OTR_HASH=0x31, BRANCH_Z_OTR=0x35, SETGRAYSCALE=0x39,
    EXPANDED=frozenset({0x20, 0x31, 0x32, 0x33, 0x35, 0x36, 0x42}),
    # Native opcodes a raw (non-OTR) F3DEX2 list may contain.
    NATIVE=frozenset({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07} | set(range(0xD3, 0x100))),
)
CYCLE_SHIFT = 32 + 20
CYCLE_COPY, CYCLE_FILL = 2, 3

# StrHash64.cpp: ECMA-182 polynomial, MSB-first table, all-ones start, no final xor.
_POLY = 0x42F0E1EBA9EA3693
_TABLE = []
for _i in range(256):
    _crc = _i << 56
    for _ in range(8):
        _crc = ((_crc << 1) ^ _POLY) & FULL if _crc & (1 << 63) else (_crc << 1) & FULL
    _TABLE.append(_crc)


def crc64(text):
    crc = FULL
    for byte in text.encode('utf-8'):
        crc = _TABLE[((crc >> 56) ^ byte) & 0xFF] ^ ((crc << 8) & FULL)
    return crc


def field(word, shift, width):
    return (word >> shift) & ((1 << width) - 1)


def color_comb(a, b, c, d):
    return (a & 0xf) | ((b & 0xf) << 4) | ((c & 0x1f) << 8) | ((d & 7) << 13)


def alpha_comb(a, b, c, d):
    return (a & 7) | ((b & 7) << 3) | ((c & 7) << 6) | ((d & 7) << 9)


def combine_mode(rgb, alpha, rgb2=0, alpha2=0):
    return rgb | (alpha << 16) | (rgb2 << 28) | (alpha2 << 44)


# GfxDpTextureRectangle (copy mode) and GfxDpFillRectangle (fill mode): G_CCMUX_TEXEL0 1, G_CCMUX_SHADE 4.
COPY_COMBINE = combine_mode(color_comb(0, 0, 0, 1), alpha_comb(0, 0, 0, 1))
FILL_COMBINE = combine_mode(color_comb(0, 0, 0, 4), alpha_comb(0, 0, 0, 4))


def decode_combine(w0, w1):
    """gfx_set_combine_handler_rdp."""
    rgb = color_comb(field(w0, 20, 4), field(w1, 28, 4), field(w0, 15, 5), field(w1, 15, 3))
    alpha = alpha_comb(field(w0, 12, 3), field(w1, 12, 3), field(w0, 9, 3), field(w1, 9, 3))
    rgb2 = color_comb(field(w0, 5, 4), field(w1, 24, 4), field(w0, 0, 5), field(w1, 6, 3))
    alpha2 = alpha_comb(field(w1, 21, 3), field(w1, 3, 3), field(w1, 18, 3), field(w1, 0, 3))
    return combine_mode(rgb, alpha, rgb2, alpha2)


def rect_combine(kind, combine, other_mode):
    """The combine a rectangle draws with: copy and fill modes replace it."""
    cycle = (other_mode >> CYCLE_SHIFT) & 3
    if kind == 'fillrect' and cycle == CYCLE_FILL:
        return FILL_COMBINE
    if kind == 'texrect' and cycle == CYCLE_COPY:
        return COPY_COMBINE
    return combine


def parse_display_list(data, opcodes=F3DEX2):
    """ResourceFactoryBinaryDisplayListV0: commands as (op, w0, w1, x0, x1)."""
    if len(data) < OTR_HEADER + 16:
        return None
    order = '<' if data[0] == 0 else '>'
    if struct.unpack_from(order + 'I', data, 4)[0] != DISPLAY_LIST:
        return None
    payload = data[OTR_HEADER:]
    if payload[0] != UCODE_F3DEX2:
        return None
    position = 8  # the ucode byte, then padding to an 8-byte boundary
    commands = []
    while position + 8 <= len(payload):
        w0, w1 = struct.unpack_from(order + 'II', payload, position)
        position += 8
        op = w0 >> 24
        x0 = x1 = 0
        if op in opcodes['EXPANDED']:
            if position + 8 > len(payload):
                break
            x0, x1 = struct.unpack_from(order + 'II', payload, position)
            position += 8
        commands.append((op, w0, w1, x0, x1))
        if op == opcodes['ENDDL']:
            break
    return commands


def raw_display_lists(data, opcodes=F3DEX2, order='>'):
    """Native F3DEX2 lists inside an opaque blob: 8-byte aligned runs of native
    opcodes ending in an exact gsSPEndDisplayList. A run may begin with data
    bytes; the extra states only add pack entries."""
    lists, run = [], []
    end = opcodes['ENDDL'] << 24
    for position in range(0, len(data) - 7, 8):
        w0, w1 = struct.unpack_from(order + 'II', data, position)
        op = w0 >> 24
        if op not in opcodes['NATIVE']:
            run = []
            continue
        run.append((op, w0, w1, 0, 0))
        if w0 == end and w1 == 0:
            if any(c[0] != 0 and c[0] != opcodes['ENDDL'] for c in run):
                lists.append(run)
            run = []
    return lists


class State:
    __slots__ = ('combine', 'other_mode', 'known', 'tiles', 'first_tile', 'grayscale', 'opcodes')

    def __init__(self, combine=None, other_mode=0, known=0, opcodes=F3DEX2):
        self.combine = combine
        self.other_mode = other_mode  # only known bits are meaningful
        self.known = known
        self.tiles = {}
        self.first_tile = None
        self.grayscale = False
        self.opcodes = opcodes

    def copy(self):
        other = State(self.combine, self.other_mode, self.known, self.opcodes)
        other.tiles = dict(self.tiles)
        other.first_tile = self.first_tile
        other.grayscale = self.grayscale
        return other

    def set_other_mode(self, shift, bits, value):
        """GfxSpSetOtherMode: the value is ORed unmasked, so its set bits are known too."""
        if shift < 0 or bits <= 0 or shift + bits > 64:
            return
        mask = ((1 << bits) - 1) << shift
        value &= FULL
        self.other_mode = (self.other_mode & ~mask) | value
        self.known |= mask | value

    def apply(self, op, w0, w1):
        o = self.opcodes
        if op == o['SETCOMBINE']:
            self.combine = decode_combine(w0, w1)
        elif op == o['SETOTHERMODE_L']:
            self.set_other_mode(31 - field(w0, 8, 8) - field(w0, 0, 8), field(w0, 0, 8) + 1, w1)
        elif op == o['SETOTHERMODE_H']:
            self.set_other_mode(63 - field(w0, 8, 8) - field(w0, 0, 8), field(w0, 0, 8) + 1, w1 << 32)
        elif op == o['RDPSETOTHERMODE']:
            self.other_mode = (field(w0, 0, 24) << 32) | w1
            self.known = FULL
        elif op == o['SETTILE']:
            self.tiles[field(w1, 24, 3)] = (field(w1, 8, 2), field(w1, 18, 2))
        elif op == o['TEXTURE']:
            self.first_tile = field(w0, 8, 3)
        elif op == o['SETGRAYSCALE']:
            self.grayscale = w1 != 0

    def record(self, kind, tile=None):
        first = tile if tile is not None else (self.first_tile or 0)
        return (kind, self.combine, self.other_mode, self.known, first,
                tuple(sorted(self.tiles.items())), self.grayscale)


def simulate(commands, state, records, resolve=None, stack=()):
    """Draw records of one command list; resolve maps an OTR hash to a nested list key."""
    o = state.opcodes
    draws = (o['TRI1'], o['TRI2'], o['QUAD'], o['TRI1_OTR'], o['TRI1_WIDE'])
    for op, w0, w1, x0, x1 in commands:
        if op in draws:
            records.add(state.record('tri'))
        elif op in (o['TEXRECT'], o['TEXRECTFLIP']):
            records.add(state.record('texrect', field(w1, 24, 3)))
        elif op in (o['TEXRECT_WIDE'], o['IMAGERECT']):
            records.add(state.record('texrect'))
        elif op == o['FILLRECT']:
            records.add(state.record('fillrect'))
        elif op == o['DL_OTR_HASH']:
            if resolve:
                resolve(((x0 << 32) | x1) & FULL, state, records, stack)
            if field(w0, 16, 1):
                return
        elif op == o['BRANCH_Z_OTR']:
            # Either outcome may occur: take the branch on a copy, then fall through.
            if resolve:
                resolve(((x0 << 32) | x1) & FULL, state.copy(), records, stack)
        elif op == o['DL']:
            # Segmented calls depend on game state; a branch ends this list.
            if field(w0, 16, 1):
                return
        elif op == o['ENDDL']:
            return
        else:
            state.apply(op, w0, w1)


# Worker globals: archive handles, a hash -> (archive, name) map and parsed lists.
_ARCHIVES = []
_NAMES = {}
_PARSED = {}
_OPCODES = F3DEX2


def _open(paths, opcodes):
    global _ARCHIVES, _NAMES, _OPCODES
    _OPCODES = opcodes
    _ARCHIVES = [zipfile.ZipFile(p) for p in paths]
    _NAMES = {}
    for index, archive in enumerate(_ARCHIVES):
        for name in archive.namelist():
            if not name.endswith('/'):
                _NAMES.setdefault(crc64(name), (index, name))


def _commands(key):
    if key not in _PARSED:
        index, name = key
        try:
            _PARSED[key] = parse_display_list(_ARCHIVES[index].read(name), _OPCODES)
        except (KeyError, zipfile.BadZipFile, struct.error):
            _PARSED[key] = None
    return _PARSED[key]


def _run(key, state, records, stack):
    commands = _commands(key)
    if not commands or key in stack or len(stack) >= MAX_DEPTH:
        return
    simulate(commands, state, records, _resolve, stack + (key,))


def _resolve(hash_value, state, records, stack):
    target = _NAMES.get(hash_value)
    if target:
        _run(target, state, records, stack)


def _simulate(entries):
    records = set()
    lists = raw = 0
    for index, name in entries:
        data = _ARCHIVES[index].read(name)
        order = '<' if data[0] == 0 else '>'
        kind = struct.unpack_from(order + 'I', data, 4)[0]
        if kind == DISPLAY_LIST:
            lists += 1
            _run((index, name), State(opcodes=_OPCODES), records, ())
        elif _RAW is not None and kind in _RAW:
            for commands in raw_display_lists(data[OTR_HEADER:], _OPCODES):
                raw += 1
                simulate(commands, State(opcodes=_OPCODES), records)
    return lists + raw, records


_RAW = None


def _initialize(paths, opcodes, raw):
    global _RAW
    _RAW = raw
    _open(paths, opcodes)


def display_list_records(archives, jobs, opcodes=F3DEX2, raw_types=None):
    """Draw records of every display list, from unknown entry state.

    raw_types: resource type magics (as in the O2R header) whose payloads are
    scanned for native display lists, such as relocatable game data files.
    Returns (lists simulated, records)."""
    entries = []
    for index, path in enumerate(archives):
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if not info.is_dir() and not info.filename.startswith('alt/') and \
                        info.file_size >= OTR_HEADER + 16:
                    entries.append((index, info.filename))
    jobs = max(1, jobs)
    chunks = [entries[i::jobs * 8] for i in range(jobs * 8)]
    records = set()
    lists = 0
    raw = frozenset(raw_types) if raw_types else None
    with Pool(jobs, initializer=_initialize, initargs=(list(map(str, archives)), opcodes, raw)) as pool:
        for count, part in pool.imap_unordered(_simulate, chunks):
            lists += count
            records |= part
    return lists, records


# ---- C sources -----------------------------------------------------------

MACROS = ('SetCombineMode', 'SetCombineLERP', 'SetRenderMode', 'SetOtherMode', 'SetCycleType', 'SetAlphaCompare')
CALL = re.compile(r'\bg(s?)DP(' + '|'.join(MACROS) + r')\s*\(')
LERP_TOKENS = {'COMBINED', 'TEXEL0', 'TEXEL1', 'PRIMITIVE', 'SHADE', 'ENVIRONMENT', 'CENTER', 'SCALE',
               'COMBINED_ALPHA', 'TEXEL0_ALPHA', 'TEXEL1_ALPHA', 'PRIMITIVE_ALPHA', 'SHADE_ALPHA', 'ENV_ALPHA',
               'LOD_FRACTION', 'PRIM_LOD_FRAC', 'NOISE', 'K4', 'K5', '0', '1'}
IDENTIFIER = re.compile(r'[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+|\d+')


def defined_macros(headers):
    names = set()
    for header in headers:
        names |= set(re.findall(r'^\s*#\s*define\s+(\w+)', Path(header).read_text(errors='replace'), re.M))
    return names


def _arguments(text, start):
    """Top-level comma-separated arguments of the call whose '(' is at start-1."""
    depth, current, args, i = 1, [], [], start
    while i < len(text) and depth:
        ch = text[i]
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if not depth:
                break
        if ch == ',' and depth == 1:
            args.append(''.join(current).strip())
            current = []
        else:
            current.append(ch)
        i += 1
    args.append(''.join(current).strip())
    return args


def _constant(arg, defined):
    tokens = IDENTIFIER.findall(arg)
    if not tokens or re.sub(r'[\w\s|()<>]', '', arg):
        return False
    return all(t[0].isdigit() or t in defined for t in tokens)


def c_calls(source_roots, defined):
    """Constant state calls as (file, gs-macro expression)."""
    found = []
    for root in source_roots:
        for path in sorted(Path(root).rglob('*')):
            if path.suffix not in ('.c', '.cpp', '.h', '.inc'):
                continue
            text = path.read_text(encoding='utf-8', errors='replace')
            text = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', text, flags=re.S)
            for match in CALL.finditer(text):
                args = _arguments(text, match.end())
                if not match.group(1):
                    args = args[1:]  # display-list pointer
                macro = match.group(2)
                if macro == 'SetCombineLERP':
                    ok = len(args) == 16 and all(a in LERP_TOKENS for a in args)
                else:
                    ok = args and all(_constant(a, defined) for a in args)
                if ok:
                    found.append((path.as_posix(), 'gsDP' + macro + '(' + ', '.join(args) + ')'))
    return found


def setup_table(z_rcp, symbol):
    """The braced initializer of the setup display-list table (e.g. sSetupDL, gSetupDLs)."""
    text = Path(z_rcp).read_text(encoding='utf-8')
    start = text.index('Gfx ' + symbol + '[')
    body = text.index('{', text.index('=', start))
    depth, i = 0, body
    while True:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                break
        i += 1
    return text[body:i + 1]


def setup_functions(z_rcp):
    """Gfx_SetupDL* function name -> SETUPDL indices it applies."""
    text = Path(z_rcp).read_text(encoding='utf-8')
    names = {}
    for match in re.finditer(r'\b(Gfx_SetupDL\w*)\s*\([^;{]*\)\s*\{', text):
        end = text.find('\n}', match.end())
        indices = {int(n) for n in re.findall(r'SETUPDL_(\d+)', text[match.end():end])}
        if indices:
            names[match.group(1)] = indices
    return names


def dump_program(table, calls, include='libultraship/libultra/gbi.h', width=6):
    """Host program printing every setup command ('S') and constant call ('C')."""
    lines = ['#include <stdint.h>', '#include <stdio.h>', '#include "' + include + '"']
    if table:
        lines.append('static Gfx setup[][%d] = %s;' % (width, table))
    lines += ['struct Call { const char* file; Gfx g; };', 'static Call calls[] = {']
    for file, expression in calls:
        lines.append('    { "' + file.replace('\\', '/') + '", ' + expression + ' },')
    lines += ['    { "", gsSPEndDisplayList() }', '};', 'int main() {']
    if table:
        lines += ['    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i)',
                  '        for (int j = 0; j < %d; ++j)' % width,
                  '            printf("S %u %08x %08x\\n", (unsigned)i, (unsigned)setup[i][j].words.w0,'
                  ' (unsigned)setup[i][j].words.w1);']
    lines += ['    for (size_t i = 0; i + 1 < sizeof(calls) / sizeof(calls[0]); ++i)',
              '        printf("C %s %08x %08x\\n", calls[i].file, (unsigned)calls[i].g.words.w0,'
              ' (unsigned)calls[i].g.words.w1);',
              '    return 0;', '}']
    return '\n'.join(lines) + '\n'


def c_seeds(dump_output, z_rcp, source_roots, opcodes=F3DEX2):
    """(setup states, seen other modes, C draw records) from the dump program's output.

    Without a setup table (z_rcp None), constant calls apply to an all-zero base."""
    setups, per_file = {}, {}
    for line in dump_output.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        w0, w1 = int(parts[2], 16), int(parts[3], 16)
        if parts[0] == 'S':
            setups.setdefault(int(parts[1]), []).append((w0 >> 24, w0, w1))
        elif parts[0] == 'C':
            per_file.setdefault(parts[1], []).append((w0 >> 24, w0, w1))
    setup_states = {}
    for index, commands in setups.items():
        state = State(combine=0, other_mode=0, known=FULL, opcodes=opcodes)
        for op, w0, w1 in commands:
            state.apply(op, w0, w1)
        setup_states[index] = (state.combine, state.other_mode)
    if not setup_states:
        setup_states[0] = (0, 0)
    functions = setup_functions(z_rcp) if z_rcp else {}
    call = re.compile(r'\b(' + '|'.join(sorted(functions, key=len, reverse=True)) + r')\s*\(') if functions else None
    modes_seen = {m for _, m in setup_states.values()}
    c_records = set()
    files = set(per_file)
    for root in source_roots:
        for path in Path(root).rglob('*'):
            if path.suffix in ('.c', '.cpp') and call:
                text = path.read_text(encoding='utf-8', errors='replace')
                if call.search(text) or 'FillRectangle' in text or 'TextureRectangle' in text:
                    files.add(path.as_posix())
    for file in files:
        text = Path(file).read_text(encoding='utf-8', errors='replace')
        indices = set()
        if call:
            for match in call.finditer(text):
                indices |= functions[match.group(1)]
        bases = [setup_states[i] for i in sorted(indices) if i in setup_states] or list(setup_states.values())
        modifiers = per_file.get(file, [])
        combines = {b[0] for b in bases} | {decode_combine(w0, w1) for op, w0, w1 in modifiers
                                            if op == opcodes['SETCOMBINE']}
        modes = {b[1] for b in bases}
        for base in list(modes):
            cumulative = State(other_mode=base, known=FULL, opcodes=opcodes)
            for op, w0, w1 in modifiers:
                if op == opcodes['SETCOMBINE']:
                    continue
                single = State(other_mode=base, known=FULL, opcodes=opcodes)
                single.apply(op, w0, w1)
                cumulative.apply(op, w0, w1)
                modes.add(single.other_mode)
                modes.add(cumulative.other_mode)
        kinds = ['tri'] + (['fillrect'] if 'FillRectangle' in text else []) + \
                (['texrect'] if 'TextureRectangle' in text else [])
        modes_seen |= modes
        for combine in combines:
            for mode in modes:
                for kind in kinds:
                    c_records.add((kind, combine, mode, FULL, 0, (), False))
    return set(setup_states.values()), modes_seen, c_records


# ---- expansion ------------------------------------------------------------

_SEEDS = []
_SEED_MODES = []
_RELEVANT = 0


def _seed(setups, modes, relevant):
    """Unknown other-mode bits take every seen C-side mode; an unknown combine
    takes the setup display lists' combines with every such mode."""
    global _SEEDS, _SEED_MODES, _RELEVANT
    _RELEVANT = relevant
    _SEED_MODES = sorted({m & relevant for m in modes})
    combines = sorted({c for c, _ in setups})
    _SEEDS = sorted({(c, m) for c in combines for m in _SEED_MODES} | {(c, m & relevant) for c, m in setups})


def _expand(records):
    found = set()
    for kind, combine, mode, known, first, tiles, gray in records:
        complete = (known & _RELEVANT) == _RELEVANT
        if combine is not None and complete:
            found.add((kind, combine, mode, gray, first, tiles))
        elif combine is not None:
            for seed_mode in _SEED_MODES:
                found.add((kind, combine, (seed_mode & ~known) | mode, gray, first, tiles))
        else:
            for seed_combine, seed_mode in _SEEDS:
                found.add((kind, seed_combine, (seed_mode & ~known) | mode, gray, first, tiles))
    return found


def expand(records, setups, modes, jobs, relevant):
    """Resolved draw states (kind, combine, other mode, grayscale, first tile, tiles).

    relevant: the other-mode bits that can change a key (a superset is safe)."""
    records = sorted({(kind, combine, mode & known & relevant, known & relevant, first, tiles, gray)
                      for kind, combine, mode, known, first, tiles, gray in records}, key=repr)
    jobs = max(1, jobs)
    chunks = [records[i::jobs * 8] for i in range(jobs * 8)]
    found = set()
    with Pool(jobs, initializer=_seed, initargs=(list(setups), list(modes), relevant)) as pool:
        for part in pool.imap_unordered(_expand, chunks):
            found |= part
    return found


def default_jobs():
    return os.cpu_count() or 1
