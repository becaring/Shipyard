"""Materialize reviewed POD initializer arguments of standalone free calls."""


def lower(text, parser, walk, *, function, types, expected):
    data = text.encode()
    if b'_callInitializer' in data:
        raise ValueError('Call initializer temporary collision')
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'call_expression':
            continue
        callee = node.child_by_field_name('function')
        if callee.text.decode() != function:
            continue
        args = [n for n in node.child_by_field_name('arguments').named_children if n.type != 'comment']
        if not any(n.type == 'initializer_list' for n in args):
            continue
        if callee.type not in ('identifier', 'qualified_identifier') or node.parent.type != 'expression_statement':
            raise ValueError('Initializer requires a standalone free call')
        if len(args) != len(types) or any(n.type != 'initializer_list' for n in args):
            raise ValueError('Call initializer signature changed')
        declarations = []
        names = []
        for index, (typ, arg) in enumerate(zip(types, args)):
            name = '_callInitializer'+str(index)
            declarations.append(typ+' '+name+' = '+arg.text.decode()+';')
            names.append(name)
        replacement = '{ '+ ' '.join(declarations)+' '+function+'('+', '.join(names)+'); }'
        edits.append((node.parent.start_byte, node.parent.end_byte, replacement.encode()))
    if len(edits) != expected:
        raise ValueError('Call initializer inventory changed: '+str(len(edits)))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()
