"""Keep indirect command cursors local across XDK optimized copy loops.

XDK 16.00.11886 can hoist an indirect cursor load out of a loop copying
union words into a local float array, then repeatedly store the same cursor.
An explicit local cursor and one writeback preserve the intended traversal.
"""


def lower_indirect_loop(text, parser, walk, *, function, parameter, cursor_type, expected_reads):
    source = text.encode()
    functions = []
    for node in walk(parser.parse(source).root_node):
        if node.type != 'function_definition':
            continue
        declaration = node.child_by_field_name('declarator')
        name = declaration.child_by_field_name('declarator')
        if name is not None and name.text.decode() == function:
            functions.append(node)
    if len(functions) != 1:
        raise ValueError('Expected one cursor function: ' + function)
    owner = functions[0]
    if b'shipyardCursor' in owner.text:
        raise ValueError('Cursor local already exists')
    indirect = ('*' + parameter).encode()
    loops = [node for node in walk(owner) if node.type == 'for_statement'
             and any(child.type == 'pointer_expression' and child.text == indirect
                     for child in walk(node))]
    if len(loops) != 1:
        raise ValueError('Expected one indirect cursor loop: ' + function)
    loop = loops[0]
    if any(node.type in ('return_statement', 'break_statement', 'continue_statement',
                         'goto_statement', 'throw_statement') for node in walk(loop)):
        raise ValueError('Cursor loop exits before writeback')
    for node in walk(loop):
        if node.type == 'call_expression' and node.child_by_field_name('function').text != b'memcpy':
            raise ValueError('Cursor loop has a call that could observe intermediate writeback')
    uses = [node for node in walk(loop)
            if node.type == 'pointer_expression' and node.text == indirect]
    if len(uses) != expected_reads:
        raise ValueError('Indirect cursor read count changed')
    # Reject aliases or mutations of the pointer parameter outside the selected
    # dereferences. Its pointee may be read/advanced; the parameter stays fixed.
    for node in walk(loop):
        if node.type == 'identifier' and node.text.decode() == parameter:
            if node.parent not in uses:
                raise ValueError('Unexpected pointer parameter use')
    body = loop.text
    for node in sorted(uses, key=lambda item: item.start_byte, reverse=True):
        first, last = node.start_byte - loop.start_byte, node.end_byte - loop.start_byte
        body = body[:first] + b'shipyardCursor' + body[last:]
    replacement = ('{\n    ' + cursor_type + ' shipyardCursor = *' + parameter + ';\n').encode()
    replacement += body
    replacement += ('\n    *' + parameter + ' = shipyardCursor;\n}').encode()
    return (source[:loop.start_byte] + replacement + source[loop.end_byte:]).decode()
