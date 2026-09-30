"""Lower reviewed two-element pair bindings for legacy C++ compilers."""


def lower_pairs(text, parser, walk, *, expected):
    data = text.encode()
    if b'_boundPair' in data:
        raise ValueError('Pair binding temporary collision')
    found = 0
    while True:
        bindings = [node for node in walk(parser.parse(data).root_node)
                    if node.type == 'structured_binding_declarator']
        if not bindings:
            break
        binding = bindings[-1]
        names = binding.named_children
        if len(names) != 2 or any(n.type != 'identifier' for n in names):
            raise ValueError('Only reviewed pair bindings are supported')
        temporary = '_boundPair'+str(found)
        owner = binding.parent
        while owner.type in ('reference_declarator', 'init_declarator'):
            owner = owner.parent
        if owner.type == 'for_range_loop':
            opening = next(c for c in owner.children if c.type == '(')
            prefix = data[opening.end_byte:binding.start_byte].decode().strip()
            body = owner.child_by_field_name('body')
            inner = body.text.decode()[1:-1] if body.type == 'compound_statement' else body.text.decode()
            aliases = ''.join('auto& '+name.text.decode()+' = '+temporary+'.'+field+'; '
                              for name, field in zip(names, ('first', 'second')))
            replacement = ('for ('+prefix+' '+temporary+' : '+owner.child_by_field_name('right').text.decode()
                           +') { '+aliases+inner+' }')
        elif owner.type == 'declaration':
            prefix = data[owner.start_byte:binding.start_byte].decode().strip()
            initializers = [c for c in owner.named_children if c.type == 'init_declarator']
            if len(initializers) != 1:
                raise ValueError('Unreviewed pair declaration')
            value = initializers[0].child_by_field_name('value')
            if value is None:
                raise ValueError('Pair binding requires an initializer')
            replacement = prefix+' '+temporary+' = '+value.text.decode()+'; '
            replacement += ''.join('auto& '+name.text.decode()+' = '+temporary+'.'+field+'; '
                                   for name, field in zip(names, ('first', 'second')))
        else:
            raise ValueError('Unreviewed pair binding context: '+owner.type)
        if prefix not in ('auto', 'auto&', 'auto &', 'const auto&', 'const auto &', 'const auto'):
            raise ValueError('Unreviewed pair binding qualifier: '+prefix)
        data = data[:owner.start_byte]+replacement.encode()+data[owner.end_byte:]
        found += 1
    if found != expected:
        raise ValueError('Pair binding inventory changed: '+str(found))
    return data.decode()
