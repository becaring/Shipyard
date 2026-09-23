"""Lower pinned Fast3D opcode tables for the XDK compiler."""

FAST3D_TABLES = ('rdpHandlers', 'otrHandlers', 'f3dex2Handlers',
                 'f3dexHandlers', 'f3dHandlers', 's2dexHandlers')


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Expected exactly one opcode anchor: ' + old[:80])
    return text.replace(old, new, 1)


def lower(text, parser, walk, expected_tables):
    start = text.index('class UcodeHandler {')
    end = text.index('\n};', start) + 3
    cls = text[start:end]
    old = '''    inline constexpr UcodeHandler(
        std::initializer_list<std::pair<int8_t, std::pair<const char*, GfxOpcodeHandlerFunc>>> initializer) {
        std::fill(std::begin(mHandlers), std::end(mHandlers),
                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));

        for (const auto& [opcode, handler] : initializer) {
            mHandlers[static_cast<uint8_t>(opcode)] = handler;
        }
    }'''
    new = '''    UcodeHandler(const UcodeEntry* initializer, size_t count) {
        std::fill(mHandlers, mHandlers + sizeof(mHandlers)/sizeof(mHandlers[0]),
                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));
        for (size_t i=0; i<count; ++i) {
            const UcodeEntry& entry=initializer[i];
            mHandlers[static_cast<uint8_t>(entry.opcode)] = std::make_pair(entry.name, entry.handler);
        }
    }'''
    cls = _once(cls, old, new)
    cls = _once(cls, 'std::numeric_limits<uint8_t>::max() + 1',
                '256 /* complete uint8_t opcode domain */')
    text = text[:start] + 'struct UcodeEntry { int8_t opcode; const char* name; GfxOpcodeHandlerFunc handler; };\n' + cls + text[end:]
    source = text.encode()
    edits = []
    tables = []
    for node in walk(parser.parse(source).root_node):
        if node.type != 'declaration' or not node.text.startswith(b'static constexpr UcodeHandler '):
            continue
        declarator = node.child_by_field_name('declarator')
        name = declarator.child_by_field_name('declarator').text.decode()
        values = declarator.child_by_field_name('value')
        rows = []
        for entry in values.named_children:
            if entry.type == 'comment':
                continue
            fields = [field for field in entry.named_children if field.type != 'comment']
            if len(fields) != 2 or fields[1].type != 'initializer_list':
                raise ValueError('Opcode entry shape drift')
            pair = [field for field in fields[1].named_children if field.type != 'comment']
            if len(pair) != 2:
                raise ValueError('Opcode pair shape drift')
            rows.append('{ ' + fields[0].text.decode() + ', ' + pair[0].text.decode() + ', ' + pair[1].text.decode() + ' }')
        if not rows:
            raise ValueError('Empty opcode table: ' + name)
        tables.append(dict(name=name, count=len(rows)))
        replacement = 'static const UcodeEntry ' + name + 'Entries[] = {\n    ' + ',\n    '.join(rows) + '\n};\n'
        replacement += 'static const UcodeHandler ' + name + '(' + name + 'Entries, sizeof(' + name + 'Entries)/sizeof(' + name + 'Entries[0]));'
        edits.append((node.start_byte, node.end_byte, replacement.encode()))
    if tuple(table['name'] for table in tables) != tuple(expected_tables):
        raise ValueError('Opcode table inventory drift: ' + repr(tables))
    for start, end, replacement in sorted(edits, reverse=True):
        source = source[:start] + replacement + source[end:]
    return source.decode(), tables
