"""Lower selected map initializer lists without changing entry order or duplicate handling."""


def lower(text, parser, walk, names):
    source = text.encode()
    edits, found = [], []
    for node in walk(parser.parse(source).root_node):
        if node.type != 'declaration':
            continue
        declarator = node.child_by_field_name('declarator')
        if declarator is None or declarator.type != 'init_declarator':
            continue
        name_node = declarator.child_by_field_name('declarator')
        if name_node is None or name_node.text.decode() not in names:
            continue
        name = name_node.text.decode()
        type_node = node.child_by_field_name('type')
        typ = type_node.text.decode()
        prefix = source[node.start_byte:type_node.start_byte].decode()
        if prefix.strip() not in ('', 'const', 'static', 'static const'):
            raise ValueError('Unreviewed map declaration qualifier')
        value = declarator.child_by_field_name('value')
        if not typ.startswith(('std::map<', 'std::unordered_map<')) or value.type != 'initializer_list':
            raise ValueError('Unreviewed map initializer: '+name)
        if not value.text[1:-1].strip():
            raise ValueError('Empty map initializer requires separate lowering')
        alias, builder = '_MapType_'+name, '_BuildMap_'+name
        replacement = ('typedef '+typ+' '+alias+';\nstatic '+alias+' '+builder+'() {\n'
            '    struct Entry { '+alias+'::key_type key; '+alias+'::mapped_type value; };\n'
            '    const Entry entries[] = '+value.text.decode()+';\n    '+alias+' result;\n'
            '    for (size_t i=0; i<sizeof(entries)/sizeof(entries[0]); ++i)\n'
            '        result.insert(std::make_pair(entries[i].key, entries[i].value));\n'
            '    return result;\n}\n'+prefix+typ+' '+name+' = '+builder+'();').encode()
        edits.append((node.start_byte, node.end_byte, replacement))
        found.append(name)
    if sorted(found) != sorted(names):
        raise ValueError('Map initializer inventory changed: '+repr(found))
    for first, last, replacement in sorted(edits, reverse=True):
        source = source[:first]+replacement+source[last:]
    return source.decode()
