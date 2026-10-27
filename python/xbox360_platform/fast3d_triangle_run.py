"""TRI2 run batching: consecutive TRI2 commands draw inside one handler call.

The handler leaves `*cmd0` on the last TRI2 it consumed, so the dispatcher's
normal advance continues after the run. Runs stop at the debugger (breakpoints
address single commands) and under USE_GBI_TRACE. TRI2 is pipeline-neutral
(fast3d_pipeline_stamps), so a run keeps its certified key.

Consumers add `enable` terms for their own per-command observers, a `stop`
expression evaluated before `cmd + 1` is read, and `advance` statements that
keep per-command accounting.
"""

HANDLER = 'bool gfx_tri2_handler_f3dex(F3DGfx** cmd0) {'
BODY = ('    gfx->GfxSpTri1(C0(17, 7), C0(9, 7), C0(1, 7), false);\n'
        '    gfx->GfxSpTri1(C1(17, 7), C1(9, 7), C1(1, 7), false);\n'
        '    return false;\n}')
RUN_START = '    g_exec_stack.start((F3DGfx*)commands);\n'
DECLARATION = '// Snapshotted per Run: breakpoints address single commands.\nstatic bool sXboxTriangleRunEnabled = false;\n'

RUN = '''    const uintptr_t xboxRunOpcode = cmd->words.w0 >> 24;
#ifdef USE_GBI_TRACE
    const bool xboxRun = false;
#else
    const bool xboxRun = sXboxTriangleRunEnabled%(enable)s;
#endif
    for (;;) {
        gfx->GfxSpTri1(C0(17, 7), C0(9, 7), C0(1, 7), false);
        gfx->GfxSpTri1(C1(17, 7), C1(9, 7), C1(1, 7), false);
        if (!xboxRun%(stop)s || ((cmd + 1)->words.w0 >> 24) != xboxRunOpcode) break;
%(advance)s        *cmd0 = ++cmd;
    }
    return false;
}'''


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('TRI2 run anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def install(text, enable='', stop='', advance=()):
    start = text.find(HANDLER)
    if start < 0 or text.count(HANDLER) != 1:
        raise ValueError('TRI2 run anchor drift: ' + HANDLER)
    end = text.index('\n}', start) + 2
    handler = _once(text[start:end], BODY, RUN % {
        'enable': ' && ' + enable if enable else '',
        'stop': ' || ' + stop if stop else '',
        'advance': ''.join('        ' + line + '\n' for line in advance),
    })
    text = text[:start] + DECLARATION + handler + text[end:]
    return _once(text, RUN_START, RUN_START + '    sXboxTriangleRunEnabled = !dbg->IsDebugging();\n')
