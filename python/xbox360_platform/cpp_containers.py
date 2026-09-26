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
        if name not in ('std::map', 'std::unordered_map', 'std::vector', 'std::set', 'std::unordered_set', 'std::pair'):
            raise ValueError('Unreviewed initialized container: '+typ)
        if len(args) != (2 if name in ('std::map', 'std::unordered_map', 'std::pair') else 1):
            raise ValueError('Unreviewed container parameters: '+typ)
        values = [child for child in node.named_children if child.type != 'comment']
        temporary = prefix+str(counter[0])
        counter[0] += 1
        if name == 'std::pair':
            if len(values) != 2:
                raise ValueError('Pair initializer must have two fields')
            first, second = emit(args[0], values[0]), emit(args[1], values[1])
            declarations.append(typ+' '+temporary+'('+first+', '+second+');')
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
