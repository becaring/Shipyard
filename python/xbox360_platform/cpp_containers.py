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
