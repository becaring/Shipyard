"""Lower reviewed JSON object assignments, retaining assignment after construction."""


def lower(text, parser, walk, *, aliases, expected):
    source = text.encode()
    edits = []
    for node in walk(parser.parse(source).root_node):
        if node.type not in ('assignment_expression', 'return_statement'):
            continue
        returning = node.type == 'return_statement'
        right = node.named_children[0] if returning and len(node.named_children) == 1 else node.child_by_field_name('right')
        if right is None:
            continue
        if right.type != 'compound_literal_expression':
            continue
        typ = right.child_by_field_name('type').text.decode()
        if typ not in aliases:
            continue
        if not returning and node.parent.type != 'expression_statement':
            raise ValueError('Unsupported JSON assignment context')
        values = right.child_by_field_name('value')
        temporary = '_jsonObject'+str(node.start_byte)
        lines = ['{ '+typ+' '+temporary+' = '+typ+'::object();']
        keys = set()
        for entry in values.named_children:
            if entry.type == 'comment':
                continue
            fields = [n for n in entry.named_children if n.type != 'comment']
            if entry.type != 'initializer_list' or len(fields) != 2 or fields[0].type != 'string_literal' or fields[1].type == 'initializer_list':
                raise ValueError('Unreviewed JSON object entry')
            key = fields[0].text.decode()
            if key in keys:
                raise ValueError('Duplicate JSON initializer key')
            keys.add(key)
            lines.append(temporary+'['+key+'] = '+fields[1].text.decode()+';')
        if not keys:
            raise ValueError('Empty JSON initialization has different semantics')
        lines.append(('return '+temporary if returning else node.child_by_field_name('left').text.decode()+' = '+temporary)+'; }')
        destination = node if returning else node.parent
        edits.append((destination.start_byte, destination.end_byte, '\n'.join(lines).encode()))
    if len(edits) != expected:
        raise ValueError('JSON object assignment inventory changed')
    for first, last, replacement in sorted(edits, reverse=True):
        source = source[:first]+replacement+source[last:]
    return source.decode()
