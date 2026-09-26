"""Checked lowering for C++ string literals beyond the XDK compiler limit."""

import ast
import hashlib
import re


_STRING = r'"(?:[^"\\]|\\.)*"'


def lower_binary_integers(text, parser):
    """Convert binary integer tokens to hex, preserving C++ integer suffixes."""
    data = text.encode()
    pending = [parser.parse(data).root_node]
    edits = []
    while pending:
        node = pending.pop()
        pending.extend(node.named_children)
        if node.type == 'user_defined_literal' and node.text.lower().startswith(b'0b'):
            raise ValueError('Unaudited binary integer literal')
        if node.type != 'number_literal' or not node.text.lower().startswith(b'0b'):
            continue
        match = re.fullmatch(rb'0[bB]([01]+)([uUlL]*)', node.text)
        if match is None or match[2].lower() not in (b'', b'u', b'l', b'ul', b'lu', b'll', b'ull', b'llu'):
            raise ValueError('Unaudited binary integer literal')
        value = ('0x%x' % int(match[1], 2)).encode() + match[2]
        edits.append((node.start_byte, node.end_byte, value))
    for start, end, value in sorted(edits, reverse=True):
        data = data[:start] + value + data[end:]
    return data.decode()


def lower_ascii_array(text, declaration, expected_length, expected_sha256):
    """Keep exact bytes while replacing a concatenated literal with integers."""
    if text.count(declaration) != 1:
        raise ValueError('Expected one pinned character-array declaration')
    begin = text.index(declaration) + len(declaration)
    match = re.match(r'\s*(?:' + _STRING + r'\s*)+;', text[begin:])
    if match is None:
        raise ValueError('Unaudited character-array initializer')
    end = begin + match.end() - 1
    initializer = text[begin:end]
    pieces = re.findall(_STRING, initializer)
    value = ''.join(ast.literal_eval(piece.replace(r'\?', '?')) for piece in pieces)
    payload = value.encode('ascii') + b'\0'
    if len(payload) != expected_length or hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise ValueError('Pinned character-array payload changed')
    values = ['    ' + ','.join(str(byte) for byte in payload[offset:offset + 32]) + ','
              for offset in range(0, len(payload), 32)]
    return text[:begin] + '{\n' + '\n'.join(values) + '\n}' + text[end:]
