"""Lower reviewed aggregate assignments while retaining aggregate initialization."""


def lower_assignments(text, parser, walk, targets):
    data = text.encode()
    edits = []
    counts = dict.fromkeys(targets, 0)
    for node in walk(parser.parse(data).root_node):
        if node.type != 'assignment_expression':
            continue
        right = node.child_by_field_name('right')
        if right is None or right.type != 'initializer_list':
            continue
        left = node.child_by_field_name('left').text.decode()
        if left not in targets or node.parent.type != 'expression_statement':
            raise ValueError('Unreviewed aggregate assignment: '+node.text.decode())
        operator = data[node.child_by_field_name('left').end_byte:right.start_byte].strip()
        if operator != b'=':
            raise ValueError('Unsupported aggregate assignment operator')
        typ, expected = targets[left]
        counts[left] += 1
        temporary = '_aggregateValue'+str(node.start_byte)
        replacement = ('{ '+typ+' '+temporary+' = '+right.text.decode()+'; '+left+' = '+temporary+'; }').encode()
        edits.append((node.parent.start_byte, node.parent.end_byte, replacement))
    if any(counts[name] != targets[name][1] for name in targets):
        raise ValueError('Aggregate assignment inventory changed: '+repr(counts))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()
