"""Lower reviewed aggregate assignments while retaining aggregate initialization."""


def lower_macro_arguments(text, parser, walk, *, function, argument, macros, type_name, expected):
    """Materialize selected initializer macros passed to standalone calls."""
    data = text.encode()
    functions = (function,) if isinstance(function, str) else tuple(function)
    if not functions or len(set(functions)) != len(functions):
        raise ValueError('Initializer call selection must be nonempty and unique')
    if b'_aggregateArgument' in data:
        raise ValueError('Aggregate argument temporary collision')
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'call_expression' or node.child_by_field_name('function').text.decode() not in functions:
            continue
        args = node.child_by_field_name('arguments').named_children
        if len(args) <= argument:
            raise ValueError('Selected call arity changed')
        value = args[argument]
        if value.type != 'call_expression':
            continue
        macro = value.child_by_field_name('function').text.decode()
        if macro not in macros:
            raise ValueError('Unreviewed initializer macro: '+macro)
        statement = node.parent
        if statement.type != 'expression_statement':
            raise ValueError('Initializer argument requires standalone call')
        body = data[statement.start_byte:value.start_byte] + b'_aggregateArgument' + data[value.end_byte:statement.end_byte]
        replacement = b'{ '+type_name.encode()+b' _aggregateArgument = '+value.text+b'; '+body+b' }'
        edits.append((statement.start_byte, statement.end_byte, replacement))
    if len(edits) != expected:
        raise ValueError('Initializer argument inventory changed: '+str(len(edits)))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()


def lower_assignments(text, parser, walk, targets, *, initializer_macros=()):
    data = text.encode()
    edits = []
    counts = dict.fromkeys(targets, 0)
    for node in walk(parser.parse(data).root_node):
        if node.type != 'assignment_expression':
            continue
        right = node.child_by_field_name('right')
        is_macro = (right is not None and right.type == 'call_expression' and
                    right.child_by_field_name('function').text.decode() in initializer_macros)
        if right is None or (right.type != 'initializer_list' and not is_macro):
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


def lower_returns(text, parser, walk, types):
    """Retain reviewed aggregate return initialization through a local value."""
    data = text.encode()
    counts = dict.fromkeys(types, 0)
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'return_statement' or len(node.named_children) != 1:
            continue
        value = node.named_children[0]
        if value.type != 'compound_literal_expression':
            continue
        typ = value.child_by_field_name('type').text.decode()
        if typ not in types:
            continue
        initializer = value.child_by_field_name('value')
        if initializer.type != 'initializer_list':
            raise ValueError('Unreviewed aggregate return')
        temporary = '_aggregateReturn'+str(node.start_byte)
        if temporary.encode() in data:
            raise ValueError('Aggregate return temporary collision')
        replacement = '{ '+typ+' '+temporary+' = '+initializer.text.decode()+'; return '+temporary+'; }'
        edits.append((node.start_byte, node.end_byte, replacement.encode()))
        counts[typ] += 1
    if counts != types:
        raise ValueError('Aggregate return inventory changed: '+repr(counts))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()


def lower_union_payloads(text, parser, walk, *, union_name, expected_members, expected_assignments):
    """Name POD payloads and retain partial aggregate zero initialization in factories."""
    data = text.encode()
    unions = [n for n in walk(parser.parse(data).root_node) if n.type == 'union_specifier'
              and n.child_by_field_name('name').text.decode() == union_name]
    if len(unions) != 1:
        raise ValueError('Expected one payload union')
    union = unions[0]
    edits, definitions, payloads = [], [], {}
    count = 0
    for member in union.child_by_field_name('body').named_children:
        if member.type != 'field_declaration':
            continue
        struct = member.child_by_field_name('type')
        if struct is None or struct.type != 'struct_specifier' or struct.child_by_field_name('name'):
            raise ValueError('Unreviewed union member')
        body = struct.child_by_field_name('body')
        fields = []
        for field in body.named_children:
            if field.type == 'comment':
                continue
            if field.type != 'field_declaration':
                raise ValueError('Non-POD payload form')
            typ = field.child_by_field_name('type').text.decode()
            for declarator in field.children_by_field_name('declarator'):
                names = [n for n in walk(declarator) if n.type == 'field_identifier']
                if len(names) != 1 or declarator.type not in ('field_identifier', 'pointer_declarator'):
                    raise ValueError('Unsupported payload declarator')
                name = names[0].text.decode()
                fields.append((typ+' '+declarator.text.decode().replace(name, ''), name))
        type_name = union_name+'Payload'+str(count)
        count += 1
        definitions.append('struct '+type_name+' '+body.text.decode()+';')
        edits.append((struct.start_byte, struct.end_byte, type_name.encode()))
        for declarator in member.children_by_field_name('declarator'):
            if declarator.type != 'field_identifier':
                raise ValueError('Unsupported union payload declarator')
            payloads[declarator.text.decode()] = (type_name, fields)
    if count != expected_members:
        raise ValueError('Union payload inventory changed')
    edits.append((union.start_byte, union.start_byte, ('\n'.join(definitions)+'\n').encode()))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    edits, factories = [], {}
    for node in walk(parser.parse(data).root_node):
        if node.type != 'assignment_expression':
            continue
        left, right = node.child_by_field_name('left'), node.child_by_field_name('right')
        if right.type != 'initializer_list' or left.type != 'field_expression':
            continue
        field = left.child_by_field_name('field').text.decode()
        if field not in payloads:
            raise ValueError('Unknown payload assignment: '+field)
        typ, fields = payloads[field]
        values = [n.text.decode() for n in right.named_children if n.type != 'comment']
        if len(values) > len(fields):
            raise ValueError('Excess payload initializer')
        name = 'Make'+typ+'_'+str(len(values))
        if any(n == '_aggregateResult' for t, n in fields):
            raise ValueError('Payload factory name collision')
        params = ', '.join(t+' const& '+n for t, n in fields[:len(values)])
        factories[name] = 'static '+typ+' '+name+'('+params+') { '+typ+' _aggregateResult = { '+', '.join(n for t, n in fields[:len(values)])+' }; return _aggregateResult; }'
        edits.append((right.start_byte, right.end_byte, (name+'('+', '.join(values)+')').encode()))
    if len(edits) != expected_assignments:
        raise ValueError('Payload assignment inventory changed')
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    result = data.decode()
    marker = 'union '+union_name
    if result.count(marker) != 1:
        raise ValueError('Payload union insertion point changed')
    return result.replace(marker, '\n'.join(factories.values())+'\n'+marker, 1)
