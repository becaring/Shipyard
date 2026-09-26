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


def lower_designated(text, parser, walk, *, function, type_name, fields, expected, argument=0, arity=1):
    """Value-initialize a reviewed argument, then apply its named fields in order."""
    data = text.encode()
    temporary = '_designatedArgument'+type_name.replace('::', '_')+'_'+''.join(c if c.isalnum() else '_' for c in function)
    if not temporary.isidentifier() or temporary.encode() in data:
        raise ValueError('Designated argument temporary collision')
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'call_expression' or node.child_by_field_name('function').text.decode() != function:
            continue
        args = [n for n in node.child_by_field_name('arguments').named_children if n.type != 'comment']
        if not any(n.type in ('initializer_list', 'compound_literal_expression') for n in args):
            continue
        if len(args) != arity or argument < 0 or argument >= arity:
            raise ValueError('Designated call arity changed')
        value = args[argument]
        if value.type == 'compound_literal_expression':
            if value.child_by_field_name('type').text.decode() != type_name:
                continue
            value = value.child_by_field_name('value')
        elif value.type != 'initializer_list':
            continue
        if node.parent.type != 'expression_statement' or value.has_error:
            raise ValueError('Designated argument requires a parsed standalone call')
        seen = []
        assignments = []
        for item in value.named_children:
            if item.type == 'comment':
                continue
            children = item.named_children
            if item.type != 'initializer_pair' or len(children) != 2 or children[0].type != 'field_designator':
                raise ValueError('Unreviewed designated argument field')
            field = children[0].text.decode().removeprefix('.')
            seen.append(field)
            assignments.append(temporary+'.'+field+' = '+children[1].text.decode()+';')
        schema = fields[len(edits)] if fields and isinstance(fields[0], (list, tuple)) and len(edits) < len(fields) else fields
        if seen != list(schema):
            raise ValueError('Designated field inventory changed: '+repr(seen))
        body = type_name+' '+temporary+' = '+type_name+'();\n'+'\n'.join(assignments)
        forwarded = [arg.text.decode() for arg in args]
        forwarded[argument] = temporary
        replacement = '{ '+body+'\n'+function+'('+', '.join(forwarded)+'); }'
        edits.append((node.parent.start_byte, node.parent.end_byte, replacement.encode()))
    if len(edits) != expected:
        raise ValueError('Designated call inventory changed: '+str(len(edits)))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()
