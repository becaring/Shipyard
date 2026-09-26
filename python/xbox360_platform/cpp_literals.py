"""Checked lowering for C++ string literals beyond the XDK compiler limit."""

import ast
import hashlib
import re


_STRING = r'"(?:[^"\\]|\\.)*"'


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
