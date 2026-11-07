"""libultraship GUI CPU patches shared by every consumer.

GUI methods lock the interpreter weak_ptr once per call site; each lock is an
interlocked reference-count round trip on the render thread every presentation.
"""

import re

LOCK = "mInterpreter.lock()"
LOCAL = "xboxInterpreter"
METHOD = re.compile(r"^[A-Za-z_][\w:<>,\s\*&]*?\b(\w+::\w+)\([^;{]*\)\s*(?:const\s*)?\{\n", re.M)


def cache_interpreter_locks(text, expected):
    """Lock the interpreter once in each method that locks it more than once.

    expected maps "Class::Method" to its lock count; drift in either direction fails.
    """
    found = {}
    pieces, last = [], 0
    for match in METHOD.finditer(text):
        start = match.end()
        end = text.find("\n}\n", start)
        if end < 0 or match.start() < last:
            continue
        body = text[start:end]
        count = body.count(LOCK)
        if count < 2:
            continue
        found[match.group(1)] = count
        pieces.append(text[last:start])
        pieces.append("    auto " + LOCAL + " = " + LOCK + ";\n" + body.replace(LOCK, LOCAL))
        last = end
    if found != expected:
        raise ValueError("Interpreter lock inventory changed: %r" % found)
    return "".join(pieces) + text[last:]
