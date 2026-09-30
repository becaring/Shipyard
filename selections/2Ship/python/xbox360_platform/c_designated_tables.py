"""Lower reviewed C static designated records and sparse enum-indexed tables."""
import re


def lower(source, parser, walk, *, records, tables, schemas, indices):
    """Selections map variable names to exact types; schemas give declaration order.

    Preprocess conditional sources first. Table extent remains highest selected
    index plus one, matching C's inferred array extent.
    """
    edits = []; seen = set()
    def fields(value, typ):
        if value.type != 'initializer_list' or value.has_error:
            raise ValueError('Unparsed designated record')
        names = schemas[typ]
        if len(set(names)) != len(names):
            raise ValueError('Duplicate record schema member')
        values = {}
        for child in value.named_children:
            if child.type == 'comment':
                continue
            if child.type != 'initializer_pair':
                raise ValueError('Mixed designated and positional record')
            key = child.child_by_field_name('designator'); val = child.child_by_field_name('value')
            if key.type != 'field_designator' or val.type == 'initializer_list':
                raise ValueError('Unreviewed nested designated record')
            name = key.text.decode().removeprefix('.')
            if name not in names or name in values:
                raise ValueError('Unknown or duplicate designated member: '+name)
            values[name] = val.text
        if not values:
            raise ValueError('Empty designated record')
        return b'{ '+b', '.join(values.get(name,b'0') for name in names)+b' }'
    # Preprocessor line markers can occur between array entries. Mask only
    # these markers in the parser view; byte offsets still address the input.
    view = re.sub(rb'^#line[^\r\n]*',lambda m:b' '*len(m[0]),source,flags=re.M)
    for node in walk(parser.parse(view).root_node):
        if node.type != 'init_declarator':
            continue
        declarator = node.child_by_field_name('declarator')
        array = declarator.type == 'array_declarator'
        name_node = declarator.child_by_field_name('declarator') if array else declarator
        if name_node.type != 'identifier':
            continue
        name = name_node.text.decode()
        if name not in records and name not in tables:
            continue
        selected = tables if array else records
        typ = node.parent.child_by_field_name('type').text.decode()
        if name not in selected or selected[name] != typ or name in seen:
            raise ValueError('Designated declaration identity changed: '+name)
        seen.add(name)
        value = node.child_by_field_name('value')
        if not array:
            replacement = fields(value,typ)
        else:
            if declarator.child_by_field_name('size') is not None or value.has_error:
                raise ValueError('Only parsed inferred designated arrays are supported')
            entries = {}
            for child in value.named_children:
                if child.type == 'comment':
                    continue
                if child.type != 'initializer_pair':
                    raise ValueError('Mixed designated and positional table')
                key = child.child_by_field_name('designator')
                if key.type != 'subscript_designator' or len(key.named_children) != 1:
                    raise ValueError('Unreviewed table designator')
                token = key.named_children[0].text.decode()
                if token not in indices:
                    raise ValueError('Unknown table index: '+token)
                index = indices[token]
                if not isinstance(index,int) or not 0 <= index < 65536 or index in entries:
                    raise ValueError('Invalid or duplicate table index')
                entries[index] = fields(child.child_by_field_name('value'),typ)
            if not entries:
                raise ValueError('Empty designated table')
            replacement = b'{\n'+b',\n'.join(entries.get(i,b'{ 0 }') for i in range(max(entries)+1))+b'\n}'
        edits.append((value.start_byte,value.end_byte,replacement))
    if seen != set(records)|set(tables):
        raise ValueError('Designated declaration inventory changed: '+repr(seen))
    for first,last,replacement in sorted(edits,reverse=True):
        source = source[:first]+replacement+source[last:]
    return source
