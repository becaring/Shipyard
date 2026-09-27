"""Lower explicitly typed designated values while retaining constructor defaults."""


def lower(text, parser, walk, *, types, calls, expected, prepare=None):
    """Types map to reviewed member names or a members/base schema.

    A leading nested initializer represents an explicitly reviewed base subobject
    whose type and members are explicitly supplied by the consumer.
    """
    data = text.encode()
    if b'_designatedValue' in data:
        raise ValueError('Designated expression temporary collision')
    edits = []
    for node in walk(parser.parse(data).root_node):
        candidates = []
        if node.type == 'compound_literal_expression':
            typ = node.child_by_field_name('type').text.decode()
            if typ in types:
                candidates.append((node, node.child_by_field_name('value'), typ))
        elif node.type == 'call_expression':
            function = node.child_by_field_name('function').text.decode()
            args = [n for n in node.child_by_field_name('arguments').named_children if n.type != 'comment']
            if function in types and len(args) == 1 and args[0].type == 'initializer_list':
                candidates.append((node, args[0], function))
            elif function in calls:
                index, typ = calls[function]
                if index < len(args) and args[index].type == 'initializer_list':
                    candidates.append((args[index], args[index], typ))
        for destination, value, typ in candidates:
            if typ not in types or value.has_error:
                raise ValueError('Unreviewed designated expression type or parse')
            seen = set()
            fields = []
            schema = types[typ]
            direct = schema['members'] if isinstance(schema, dict) else schema
            base_schema = schema.get('base') if isinstance(schema, dict) else None
            def read_fields(initializer, base=False):
                members = [n for n in initializer.named_children if n.type != 'comment']
                for index, member in enumerate(members):
                    if member.type == 'initializer_list' and index == 0 and not base:
                        if base_schema is None:
                            raise ValueError('Unreviewed base subobject')
                        read_fields(member, True)
                        continue
                    if member.type != 'initializer_pair':
                        raise ValueError('Unreviewed positional initializer')
                    designator = member.child_by_field_name('designator')
                    assigned = member.child_by_field_name('value')
                    if designator.type != 'field_designator' or assigned.type == 'initializer_list':
                        raise ValueError('Unreviewed designated field form')
                    field = designator.text.decode().removeprefix('.')
                    allowed = base_schema[1] if base else direct
                    identity = (base, field)
                    if field not in allowed or identity in seen:
                        raise ValueError('Unreviewed or repeated field: '+field)
                    seen.add(identity)
                    fields.append((field, assigned.text.decode(), base))
            read_fields(value)
            if not fields:
                raise ValueError('Empty initializer requires an explicit constructor review')
            local = False
            owner = destination.parent
            while owner is not None:
                if owner.type in ('function_definition', 'lambda_expression'):
                    local = True
                    break
                owner = owner.parent
            name = '_designatedValue'+str(destination.start_byte)
            statements = [typ+' '+name+' = '+typ+'();']
            if prepare is not None:
                # A version adapter may restore effects of a member default that
                # the original explicit initializer bypasses.
                statements.extend(prepare(typ, [(base,field) for field,_,base in fields], name))
            statements += [('static_cast<'+base_schema[0]+'&>('+name+')' if base else name)+'.'+field+' = '+value+';'
                           for field, value, base in fields]
            statements.append('return '+name+';')
            capture = '[&]' if local else '[]'
            replacement = '('+capture+'() -> '+typ+' { '+' '.join(statements)+' }())'
            edits.append((destination.start_byte, destination.end_byte, replacement.encode()))
    edits.sort()
    if len(edits) != expected or any(a[1] > b[0] for a,b in zip(edits, edits[1:])):
        raise ValueError('Designated expression inventory changed or overlaps: '+str(len(edits)))
    for first,last,replacement in reversed(edits):
        data = data[:first]+replacement+data[last:]
    return data.decode()
