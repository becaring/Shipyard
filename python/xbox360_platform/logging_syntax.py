"""Shared C++ syntax and source mapping for the Xbox logging adapter."""

import difflib
import hashlib
from pathlib import Path

from .prism import braces as lower_braces
from .prism import enum_wrappers
from .prism import walk


class LoggingSyntax:
    def __init__(self, root, parser):
        self.root = Path(root)
        self.parser = parser

    def braces(self, text):
        return lower_braces(text, self.parser)

    def enum_wrappers(self, text):
        return enum_wrappers(text)

    def lower_ranges(self, source, *, begin, end):
        for _ in range(64):
            edits = []
            for node in walk(self.parser.parse(source).root_node):
                if node.type != 'for_range_loop':
                    continue
                if any(child.type == 'for_range_loop' for child in list(walk(node))[1:]):
                    continue
                right = node.child_by_field_name('right')
                body = node.child_by_field_name('body')
                declarator = node.child_by_field_name('declarator')
                if right is None or body is None or declarator is None or right.type == 'initializer_list':
                    raise ValueError('Unsupported logging range loop')
                opening = next(child for child in node.children if child.type == '(')
                declaration = source[opening.end_byte:declarator.end_byte].decode().strip()
                if '[' in declaration:
                    raise ValueError('Unsupported logging structured binding')
                suffix = str(node.start_byte)
                collection = '_loggingRange' + suffix
                iterator = '_loggingIt' + suffix
                stop = '_loggingEnd' + suffix
                inner = (body.text[1:-1] if body.type == 'compound_statement'
                         else body.text).decode()
                replacement = ('{ auto&& ' + collection + ' = ' + right.text.decode() +
                               '; auto ' + iterator + ' = ' + begin + '(' + collection +
                               '); auto ' + stop + ' = ' + end + '(' + collection +
                               '); for (; ' + iterator + ' != ' + stop + '; ++' +
                               iterator + ') { ' + declaration + ' = *' + iterator +
                               '; ' + inner + ' } }').encode()
                edits.append((node.start_byte, node.end_byte, replacement))
            if not edits:
                return source
            for first, last, replacement in sorted(edits, reverse=True):
                source = source[:first] + replacement + source[last:]
        raise ValueError('Unexpected logging range nesting')

    def mapped(self, source_path, text, output):
        source = Path(source_path)
        output = Path(output)
        original = source.read_text(encoding='utf-8-sig').splitlines(keepends=True)
        changed = text.splitlines(keepends=True)
        origins = {}
        for block in difflib.SequenceMatcher(None, original, changed, autojunk=False).get_matching_blocks():
            for index in range(block.size):
                origins[block.b + index] = block.a + index + 1
        destination = output.relative_to(self.root).as_posix()
        result = []
        if output.suffix == '.h':
            guard = 'SHIPYARD_PRISM_' + hashlib.sha256(destination.encode()).hexdigest().upper()
            result.extend(('#ifndef ' + guard + '\n', '#define ' + guard + '\n'))
        previous_path = None
        previous_line = 0
        in_comment = False
        quote = None
        continuation = False
        for index, line in enumerate(changed):
            path = source.as_posix() if index in origins else destination
            number = origins.get(index, index + 1)
            if (not in_comment and quote is None and not continuation and
                    (path != previous_path or number != previous_line + 1)):
                result.append('#line %d "%s"\n' % (number, path))
            result.append(line)
            previous_path, previous_line = path, number
            cursor = 0
            while cursor < len(line):
                pair = line[cursor:cursor + 2]
                char = line[cursor]
                if in_comment:
                    if pair == '*/':
                        in_comment = False
                        cursor += 2
                        continue
                elif quote is not None:
                    if char == '\\':
                        cursor += 2
                        continue
                    if char == quote:
                        quote = None
                elif pair == '//':
                    break
                elif pair == '/*':
                    in_comment = True
                    cursor += 2
                    continue
                elif char in ('"', "'"):
                    quote = char
                cursor += 1
            continuation = line.rstrip('\r\n').endswith('\\')
        if output.suffix == '.h':
            result.append('\n#endif\n')
        output.parent.mkdir(parents=True, exist_ok=True)
        content = ''.join(result).encode()
        if not output.is_file() or output.read_bytes() != content:
            output.write_bytes(content)
