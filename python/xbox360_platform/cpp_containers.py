"""Construct reviewed nested STL initializer trees without initializer_list."""


def template_parts(type_name):
    if '<' not in type_name:
        return type_name.strip(), []
    first = type_name.index('<')
    if not type_name.endswith('>'):
        raise ValueError('Unreviewed container type')
    name, inner = type_name[:first].strip(), type_name[first+1:-1]
    args, start, depth = [], 0, 0
    for index, char in enumerate(inner):
        depth += (char == '<') - (char == '>')
        if depth < 0:
            raise ValueError('Unbalanced container type')
        if char == ',' and depth == 0:
            args.append(inner[start:index].strip())
            start = index+1
    if depth:
        raise ValueError('Unbalanced container type')
    args.append(inner[start:].strip())
    return name, args


def materialize(type_name, initializer, *, prefix):
    """Return declarations and a result name for an AST initializer_list node."""
    declarations = []
    counter = [0]

    def emit(typ, node):
        if node.type != 'initializer_list':
            return node.text.decode()
        name, args = template_parts(typ)
        if name not in ('std::map', 'std::unordered_map', 'std::vector', 'std::set', 'std::unordered_set', 'std::pair', 'std::tuple'):
            raise ValueError('Unreviewed initialized container: '+typ)
        if name == 'std::tuple':
            if not 1 <= len(args) <= 10:
                raise ValueError('Tuple arity exceeds the legacy STL contract')
        elif len(args) != (2 if name in ('std::map', 'std::unordered_map', 'std::pair') else 1):
            raise ValueError('Unreviewed container parameters: '+typ)
        values = [child for child in node.named_children if child.type != 'comment']
        temporary = prefix+str(counter[0])
        counter[0] += 1
        if name in ('std::pair', 'std::tuple'):
            if len(values) != len(args):
                raise ValueError('Pair or tuple initializer must supply every field')
            fields = [emit(arg, value) for arg, value in zip(args, values)]
            declarations.append(typ+' '+temporary+'('+', '.join(fields)+');')
        else:
            declarations.append(typ+' '+temporary+';')
            for value in values:
                if name in ('std::map', 'std::unordered_map'):
                    item = emit('std::pair<'+', '.join(args)+'>', value)
                    method = 'insert'
                else:
                    item = emit(args[0], value)
                    method = 'push_back' if name == 'std::vector' else 'insert'
                declarations.append(temporary+'.'+method+'('+item+');')
        return temporary

    result = emit(type_name, initializer)
    return '\n'.join(declarations), result


def lower_constructor_arguments(text, parser, walk, *, constructor, argument, type_name, expected):
    """Materialize container arguments of selected namespace-scope registrations."""
    data = text.encode()
    edits = []
    count = 0
    for node in walk(parser.parse(data).root_node):
        if node.type != 'declaration' or not node.child_by_field_name('type'):
            continue
        if node.child_by_field_name('type').text.decode() != constructor:
            continue
        declaration = node.child_by_field_name('declarator')
        value = declaration.child_by_field_name('value') if declaration else None
        if value is None or value.type != 'argument_list':
            raise ValueError('Unsupported registration initializer')
        args = [n for n in value.named_children if n.type != 'comment']
        if len(args) <= argument or args[argument].type != 'initializer_list':
            continue
        ancestor = node.parent
        while ancestor:
            if ancestor.type == 'function_definition':
                raise ValueError('Container registration must have namespace scope')
            ancestor = ancestor.parent
        name = '_BuildConstructorArgument'+str(node.start_byte)
        if name.encode() in data:
            raise ValueError('Constructor argument helper collision')
        body, result = materialize(type_name, args[argument], prefix='_argumentValue')
        helper = 'static '+type_name+' '+name+'() {\n'+body+'\nreturn '+result+';\n}\n'
        edits.append((node.start_byte, node.start_byte, helper.encode()))
        edits.append((args[argument].start_byte, args[argument].end_byte, (name+'()').encode()))
        count += 1
    if count != expected:
        raise ValueError('Constructor argument inventory changed: '+str(count))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode()


_TABLES = ('std::map', 'std::unordered_map', 'std::set', 'std::unordered_set', 'std::vector')


def lower_table_initializers(text, parser, walk):
    """Build brace-initialized STL tables from a static aggregate array.

    The entry text is kept verbatim: a map entry {key, value} initializes an
    aggregate {first, second}, and insertion keeps the first of repeated keys as
    initializer_list construction does. Entries may not name enclosing locals.
    """
    data = text.encode()
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'declaration':
            continue
        typ = node.child_by_field_name('type')
        declarator = node.child_by_field_name('declarator')
        if typ is None or declarator is None or declarator.type != 'init_declarator':
            continue
        value = declarator.child_by_field_name('value')
        if value is None or value.type != 'initializer_list' or not typ.text.decode().endswith('>'):
            continue
        kind, args = template_parts(typ.text.decode())
        if kind not in _TABLES:
            continue
        if len(node.children_by_field_name('declarator')) != 1:
            raise ValueError('Table declaration must declare one name')
        name = declarator.child_by_field_name('declarator').text.decode()
        type_name = typ.text.decode()
        prefix = data[node.start_byte:typ.start_byte].decode()
        if not value.named_children:
            edits.append((node.start_byte, node.end_byte, (prefix + type_name + ' ' + name + ';').encode()))
            continue
        if kind in ('std::map', 'std::unordered_map'):
            entry = 'struct entry { ' + args[0] + ' first; ' + args[1] + ' second; };\n'
            fill = ('for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) '
                    'result.insert(std::make_pair(entries[i].first, entries[i].second));\n')
            element = 'entry'
        else:
            entry = ''
            method = 'push_back' if kind == 'std::vector' else 'insert'
            fill = 'for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) result.' + method + '(entries[i]);\n'
            element = args[0]
        table = name + '_table'
        if table.encode() in data:
            raise ValueError('Table builder collision: ' + table)
        replacement = ('struct ' + table + ' { static ' + type_name + ' build() {\n' + entry +
                       'static const ' + element + ' entries[] = ' + value.text.decode() + ';\n' +
                       type_name + ' result;\n' + fill + 'return result;\n} };\n' +
                       prefix + type_name + ' ' + name + ' = ' + table + '::build();')
        edits.append((node.start_byte, node.end_byte, replacement.encode()))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first] + replacement + data[last:]
    return data.decode()
