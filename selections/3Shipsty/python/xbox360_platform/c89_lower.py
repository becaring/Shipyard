"""Lower C99 block and for declarations for the XDK C compiler."""

import re
import struct

from tree_sitter import Language, Parser
import tree_sitter_c


PARSER = Parser(Language(tree_sitter_c.language()))


def upstream_source(name: bytes) -> bool:
    return b'/upstream/' in name and name.endswith((b'.c', b'.inc'))


def lower(source: bytes, *, active_source=upstream_source,
          float_union_type: bytes | None = None) -> tuple[bytes, int]:
    # Mask non-source spans only in the parser view. XDK intrinsic headers are
    # not valid tree-sitter C input, and byte offsets must remain unchanged.
    if b'#line ' in source:
        view = bytearray(source)
        offset = 0
        active = False
        for line in source.splitlines(keepends=True):
            marker = re.match(rb'#line \d+ "([^"]+)"', line)
            if marker:
                name = re.sub(rb'\\+', b'/', marker[1].lower())
                active = active_source(name)
            if not active or marker:
                view[offset:offset+len(line)] = re.sub(rb'[^\r\n]', b' ', line)
            offset += len(line)
        parse_source = bytes(view)
    else:
        parse_source = source
    tree = PARSER.parse(parse_source)
    inserts = {}
    edits = []
    count = 0

    def insert(at, text):
        inserts[at] = inserts.get(at, b'') + text

    def visit(node):
        nonlocal count
        if node.type == 'initializer_pair' and float_union_type is not None:
            designator = node.child_by_field_name('designator')
            value = node.child_by_field_name('value')
            declaration = node.parent
            while declaration and declaration.type != 'declaration':
                declaration = declaration.parent
            decl_type = declaration.child_by_field_name('type') if declaration else None
            if (designator and source[designator.start_byte:designator.end_byte] == b'.f'
                    and decl_type and source[decl_type.start_byte:decl_type.end_byte] == float_union_type):
                token = source[value.start_byte:value.end_byte].strip()
                token = re.sub(rb'[()\s]', b'', token)
                if not re.fullmatch(rb'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?', token):
                    raise ValueError('Nonliteral float union initializer: ' + repr(token))
                bits = struct.unpack('>I', struct.pack('>f', float(token.rstrip(b'fF'))))[0]
                edits.append((node.start_byte, node.end_byte, f'0x{bits:08X}U'.encode()))
                count += 1
                return
        if node.type == 'compound_statement':
            had_statement = False
            scopes = 0
            for child in node.named_children:
                if child.type in ('comment', 'preproc_call'):
                    continue
                if child.type in ('declaration', 'type_definition'):
                    if had_statement:
                        insert(child.start_byte, b'{ ')
                        scopes += 1
                        count += 1
                        had_statement = False
                else:
                    had_statement = True
            if scopes:
                insert(node.end_byte - 1, b'} ' * scopes)
        if node.type == 'for_statement':
            init = node.child_by_field_name('initializer')
            if init is not None and init.type == 'declaration':
                # Keep original newlines for #line diagnostics in the loop body.
                decl = source[init.start_byte:init.end_byte]
                insert(node.start_byte, b'{ ' + decl + b' ')
                edits.append((init.start_byte, init.end_byte, b';'))
                insert(node.end_byte, b' }')
                count += 1
        for child in node.named_children:
            visit(child)

    visit(tree.root_node)
    edits.extend((at, at, text) for at, text in inserts.items())
    for start, end, text in sorted(edits, reverse=True):
        source = source[:start] + text + source[end:]
    return source, count
