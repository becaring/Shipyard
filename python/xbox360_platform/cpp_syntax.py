"""Compiler-compatibility lowering shared by active Xbox consumers."""


def lower_if_initializers(text, parser, walk, expected=None, max_depth=16):
    data = text.encode()
    converted = 0
    for _ in range(max_depth):
        edits = []
        for node in walk(parser.parse(data).root_node):
            if node.type != 'if_statement':
                continue
            condition = node.child_by_field_name('condition')
            initializer = condition.child_by_field_name('initializer') if condition else None
            if not initializer:
                continue
            if any(
                child.type == 'if_statement'
                and child.child_by_field_name('condition')
                and child.child_by_field_name('condition').child_by_field_name('initializer')
                for child in list(walk(node))[1:]
            ):
                continue
            value = condition.child_by_field_name('value')
            replacement = b'{ ' + initializer.text + b' if (' + value.text + b')' + data[condition.end_byte:node.end_byte] + b' }'
            edits.append((node.start_byte, node.end_byte, replacement))
        if not edits:
            if expected is not None and converted != expected:
                raise ValueError('If-initializer inventory drift: ' + str(converted))
            return data.decode()
        converted += len(edits)
        for start, end, replacement in sorted(edits, reverse=True):
            data = data[:start] + replacement + data[end:]
    raise ValueError('Unexpected nested if-initializer depth')
