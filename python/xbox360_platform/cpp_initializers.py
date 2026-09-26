"""Lower reviewed STL declaration initializers without moving their evaluation scope."""
from .cpp_containers import materialize, template_parts


def lower_lookup_keys(text, parser, walk, lookups):
    """Construct reviewed pair/tuple lookup keys without initializer_list syntax."""
    data = text.encode()
    counts = dict.fromkeys(lookups, 0)
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'call_expression':
            continue
        function = node.child_by_field_name('function').text.decode()
        if function not in lookups:
            continue
        args = [n for n in node.child_by_field_name('arguments').named_children if n.type != 'comment']
        if len(args) != 1 or args[0].type != 'initializer_list':
            raise ValueError('Unreviewed lookup key form: '+function)
        if args[0].has_error:
            raise ValueError('Lookup key initializer is not completely parsed')
        typ, expected = lookups[function]
        kind, fields = template_parts(typ)
        values = [n for n in args[0].named_children if n.type != 'comment']
        if kind not in ('std::pair', 'std::tuple') or len(values) != len(fields):
            raise ValueError('Lookup key arity changed')
        if any(n.type in ('initializer_list', 'initializer_pair') for n in values):
            raise ValueError('Nested lookup key initializer requires explicit materialization')
        replacement = typ+'('+', '.join(n.text.decode() for n in values)+')'
        edits.append((args[0].start_byte, args[0].end_byte, replacement.encode()))
        counts[function] += 1
    if any(counts[name] != lookups[name][1] for name in lookups):
        raise ValueError('Lookup key inventory changed: '+repr(counts))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()


def lower(text, parser, walk, declarations):
    data = text.encode()
    if b'_containerInitializer' in data:
        raise ValueError('Container initializer temporary collision')
    counts = dict.fromkeys(declarations, 0)
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'init_declarator' or node.parent.type != 'declaration':
            continue
        name_node = node.child_by_field_name('declarator')
        if name_node.type != 'identifier' or name_node.text.decode() not in declarations:
            continue
        name = name_node.text.decode()
        typ = node.parent.child_by_field_name('type').text.decode()
        expected_type, expected_count = declarations[name]
        value = node.child_by_field_name('value')
        if typ != expected_type or value.type != 'initializer_list':
            raise ValueError('Unreviewed container declaration: '+name)
        statements, result = materialize(typ, value, prefix='_containerInitializer')
        scope = node.parent
        local = False
        while scope.parent is not None:
            if scope.type in ('function_definition', 'lambda_expression'):
                local = True
                break
            if scope.type in ('class_specifier', 'struct_specifier'):
                raise ValueError('Container declaration requires a function or namespace scope')
            scope = scope.parent
        capture = '[&]' if local else '[]'
        expression = capture+'() -> '+typ+' {\n'+statements+'\nreturn '+result+';\n}()'
        if not any(child.type != 'comment' for child in value.named_children):
            expression = typ+'()'
        replacement = name+' = '+expression
        edits.append((node.start_byte, node.end_byte, replacement.encode()))
        counts[name] += 1
    if any(counts[name] != declarations[name][1] for name in declarations):
        raise ValueError('Container declaration inventory changed: '+repr(counts))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()
