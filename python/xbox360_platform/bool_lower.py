"""Fail closed on unreviewed C boolean operations before XDK C compilation.

The initial supported slice contains boolean declarations in included headers,
but no boolean operations in the selected implementation. A byte has the same
layout as C _Bool; conversion sites must be explicitly lowered before a unit
that uses them can be admitted.
"""

from __future__ import annotations

import re
from collections.abc import Callable

from tree_sitter import Language, Parser
import tree_sitter_c


PARSER = Parser(Language(tree_sitter_c.language()))
LINE = re.compile(rb'^#line\s+\d+\s+"([^"]+)"')
BOOL = re.compile(rb'\b_Bool\b')


def lower(source: bytes, *, active_source: Callable[[bytes], bool],
          approved_calls: frozenset[bytes] = frozenset()) -> tuple[bytes, dict[str, int]]:
    view = bytearray(source)
    selected = bytearray(len(source))
    offset = 0
    active = False
    sdk = False
    for line in source.splitlines(keepends=True):
        marker = LINE.match(line)
        if marker:
            path = marker.group(1).replace(b'\\', b'/').lower()
            active = active_source(path)
            sdk = b'microsoft xbox 360 sdk/' in path
        end = offset + len(line)
        if sdk or marker:
            view[offset:end] = re.sub(rb'[^\r\n]', b' ', line)
        if active and not marker:
            selected[offset:end] = b'\x01' * len(line)
        offset = end

    positions = {match.start() for match in BOOL.finditer(view)}
    for start in positions:
        view[start:start + 5] = b'bool '
    tree = PARSER.parse(bytes(view))
    recognized: set[int] = set()
    selected_bool: set[int] = set()
    bool_fields: set[bytes] = set()
    bool_parameter_functions: set[bytes] = set()
    selected_field_uses: set[bytes] = set()
    selected_calls: set[bytes] = set()

    def token(node) -> bytes:
        return source[node.start_byte:node.end_byte]

    def visit(node) -> None:
        if node.start_byte in positions and node.type in ('type_identifier', 'primitive_type'):
            recognized.add(node.start_byte)
            if selected[node.start_byte]:
                selected_bool.add(node.start_byte)
            if node.parent and node.parent.type == 'field_declaration':
                for declarator in node.parent.children_by_field_name('declarator'):
                    name = declarator
                    while name and name.type not in ('field_identifier', 'identifier'):
                        name = name.child_by_field_name('declarator')
                    if name:
                        bool_fields.add(token(name))
            if node.parent and node.parent.type == 'parameter_declaration':
                owner = node.parent.parent
                while owner and owner.type != 'function_declarator':
                    owner = owner.parent
                if owner:
                    name = owner.child_by_field_name('declarator')
                    while name and name.type not in ('identifier', 'field_identifier'):
                        name = name.child_by_field_name('declarator')
                    if name:
                        bool_parameter_functions.add(token(name))
        if selected[node.start_byte] and node.type == 'field_expression':
            field = node.child_by_field_name('field')
            if field:
                selected_field_uses.add(token(field))
        if selected[node.start_byte] and node.type == 'call_expression':
            function = node.child_by_field_name('function')
            if function and function.type == 'identifier':
                selected_calls.add(token(function))
        for child in node.named_children:
            visit(child)

    visit(tree.root_node)
    if recognized != positions:
        raise ValueError(f'Unparsed _Bool type tokens: {len(positions - recognized)}')
    if selected_bool:
        raise ValueError(f'Boolean operations in selected source need conversion lowering: {len(selected_bool)}')
    field_overlap = bool_fields & selected_field_uses
    if field_overlap:
        raise ValueError('Selected source accesses boolean fields: ' + repr(sorted(field_overlap)[:8]))
    call_overlap = bool_parameter_functions & selected_calls
    if approved_calls - call_overlap:
        raise ValueError('Approved boolean calls no longer match selected source: ' +
                         repr(sorted(approved_calls - call_overlap)[:8]))
    call_overlap -= approved_calls
    if call_overlap:
        raise ValueError('Selected source calls boolean-parameter functions: ' + repr(sorted(call_overlap)[:8]))

    for start in sorted(positions, reverse=True):
        source = source[:start] + b'unsigned char' + source[start + 5:]
    return source, {'type_tokens': len(positions), 'selected_bool_operations': 0,
                    'approved_boolean_calls': len(approved_calls)}
