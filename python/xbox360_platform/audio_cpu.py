"""Audio-thread CPU projections shared by every consumer.

Synthesis controls: each synthesis call reads its CVars once, on the thread that
runs it, and every per-note or per-update reader inside that call returns the
frozen value. Outside a call (or on another thread) readers stay live. CVar names
and types are per game; consumers pass them as (name, C type, live expression).

SoundMatrixDecoder: the four all-pass chains share one sweep frequency per sample,
so the coefficient is memoized on the exact double it is computed from.
"""

import re

BEGIN = "ShipyardAudioControls_Begin"
END = "ShipyardAudioControls_End"


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Audio CPU anchor drift: " + old[:90])
    return text.replace(old, new)


def _check(controls):
    if not controls:
        raise ValueError("Audio controls need at least one CVar")
    for name, kind, expression in controls:
        if not re.fullmatch(r"[A-Za-z_]\w*", name) or not kind or not expression:
            raise ValueError("Audio control declaration: " + repr(name))


def declarations(controls):
    """Prototypes for translation units that only read frozen controls."""
    _check(controls)
    return "".join("%s ShipyardAudioControl_%s(void);\n" % (kind, name) for name, kind, _ in controls)


def definitions(controls):
    """Thread-owned frozen values, Begin/End and one reader per control (C or C++)."""
    _check(controls)
    text = "static __declspec(thread) int shipyardAudioControlsActive;\n"
    for name, kind, _ in controls:
        text += "static __declspec(thread) %s shipyardAudioControl%s;\n" % (kind, name)
    text += "void %s(void) {\n" % BEGIN
    for name, _, expression in controls:
        text += "    shipyardAudioControl%s = %s;\n" % (name, expression)
    text += "    shipyardAudioControlsActive = 1;\n}\n"
    text += "void %s(void) { shipyardAudioControlsActive = 0; }\n" % END
    for name, kind, expression in controls:
        text += ("%s ShipyardAudioControl_%s(void) {\n"
                 "    return shipyardAudioControlsActive ? shipyardAudioControl%s : %s;\n}\n"
                 % (kind, name, name, expression))
    return text


def read(text, expression, name, count=1):
    """Replace exactly `count` live reads with the frozen reader."""
    if text.count(expression) != count:
        raise ValueError("Audio control reader drift: " + expression)
    return text.replace(expression, "ShipyardAudioControl_%s()" % name)


def _call(function):
    # Self-declaring block: valid C89 anywhere a statement is, and adds no lines.
    return "{ extern void %s(void); %s(); } " % (function, function)


def scope(text, begin, end):
    """Freeze before the `begin` statement; thaw before the `end` statement.

    Both anchors must be statements of the synthesis call, with every early
    return before `begin`. Inserted calls keep the source line count.
    """
    text = once(text, begin, _call(BEGIN) + begin)
    return once(text, end, _call(END) + end)


def matrix(text):
    """Memoize the all-pass coefficient in LUS SoundMatrixDecoder.cpp."""
    old = "    double c = (1.0 - chain.Freq) / (1.0 + chain.Freq);"
    new = r"""    // Four chains advance the same frequency; retain double arithmetic exactly.
    static __declspec(thread) bool coefficientReady = false;
    static __declspec(thread) double coefficientFrequency;
    static __declspec(thread) double coefficientValue;
    if (!coefficientReady || coefficientFrequency != chain.Freq) {
        coefficientFrequency = chain.Freq;
        coefficientValue = (1.0 - chain.Freq) / (1.0 + chain.Freq);
        coefficientReady = true;
    }
    double c = coefficientValue;"""
    return once(text, old, new)
