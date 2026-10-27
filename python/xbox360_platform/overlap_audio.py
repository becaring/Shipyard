"""Game-side audio barriers for synthesis that runs beside game logic.

The game submits synthesis right after its per-frame audio update and drains it
before publishing. Between those points the audio thread owns its shared state,
so every game-side function that reads or writes that state, queues an audio
thread command, or calls into the audio-thread library first waits for the
pending synthesis. The barrier is a no-op once drained, so only the first one
of a frame can wait.

The inventory is derived from the sources with tree-sitter and is a superset:
a textual state reference anywhere in a body (including dead code) counts.
"""

import re

from tree_sitter import Language, Parser
import tree_sitter_c

# A declaration, so the entry stays valid C89 ahead of the body's own declarations.
BARRIER = "const int shipyardAudioBarrier = Shipyard_OverlapAudioBarrier();"
_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def functions(text):
    """Return [(name, start, end, body_start)] byte offsets for each C definition."""
    raw = text.encode()
    stack = [Parser(Language(tree_sitter_c.language())).parse(raw).root_node]
    found = []
    while stack:
        node = stack.pop()
        if node.type == "function_definition":
            declarator = node.child_by_field_name("declarator")
            body = node.child_by_field_name("body")
            match = _CALL.search(declarator.text.decode()) if declarator is not None else None
            if match and body is not None:
                found.append((match.group(1), node.start_byte, node.end_byte, body.start_byte))
            continue
        stack.extend(node.named_children)
    return sorted(found, key=lambda item: item[1])


def _bodies(text):
    raw = text.encode()
    return {name: raw[body:end].decode(errors="replace") for name, _, end, body in functions(text)}


def touching(game_sources, library_sources, *, state=("gAudioCtx",),
             commands=("AUDIOCMD_", "AudioThread_QueueCmd"), benign=()):
    """Names of game-side functions that touch audio-thread state.

    A body touches it when it names a state symbol or command form, or calls a
    library function outside `benign`. Sources map file name to text.
    """
    library = set()
    for text in library_sources.values():
        library.update(name for name, _, _, _ in functions(text))
    words = tuple(state) + tuple(commands)
    found = set()
    for text in game_sources.values():
        for name, body in _bodies(text).items():
            if any(word in body for word in words):
                found.add(name)
                continue
            if any(call in library and call not in benign and call != name for call in _CALL.findall(body)):
                found.add(name)
    return found


def barriers(text, names, call=BARRIER):
    """Insert `call` at the entry of each named definition; return (text, inserted names)."""
    raw = text.encode()
    inserted = set()
    for name, _, _, body in reversed(functions(text)):
        if name not in names:
            continue
        if name in inserted:
            raise ValueError("Audio barrier function defined twice: " + name)
        raw = raw[:body + 1] + ("\n    " + call).encode() + raw[body + 1:]
        inserted.add(name)
    return raw.decode(), inserted
