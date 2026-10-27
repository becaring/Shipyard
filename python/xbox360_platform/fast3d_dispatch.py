"""Pin one interpreter owner and snapshot the debugger while a version-locked command walk dispatches."""

import re


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Fast3D dispatch contract changed: ' + old[:80])
    return text.replace(old, new, 1)


def owner(text, *, handlers, external_borrows):
    text = '#include "fast3d/dispatch_owner.h"\n' + text
    text = _once(text, 'static std::weak_ptr<Interpreter> mInstance;', '''static std::weak_ptr<Interpreter> mInstance;
typedef Shipyard::Fast3D::DispatchOwner<Interpreter> XboxDispatchOwner;
static __declspec(thread) XboxDispatchOwner* sXboxDispatchOwner = 0;
static __forceinline Interpreter* XboxDispatchInterpreter() {
    if (sXboxDispatchOwner) return sXboxDispatchOwner->Get(mInstance);
    return XboxDispatchOwner::TemporaryBorrow(mInstance);
}''')
    text = _once(text, '    mInstance = gfx;',
        '    mInstance = gfx;\n    XboxDispatchOwner::Invalidate(sXboxDispatchOwner);')
    anchor = 'Interpreter* gfx = mInstance.lock().get();'
    if text.count(anchor) != handlers:
        raise ValueError('Fast3D handler ownership inventory changed')
    text = text.replace(anchor, 'Interpreter* gfx = XboxDispatchInterpreter();')
    if text.count('mInstance.lock().get()') != external_borrows:
        raise ValueError('Fast3D external ownership inventory changed')
    text = _once(text, '    g_exec_stack.start((F3DGfx*)commands);', '''    {
    XboxDispatchOwner dispatchOwner(sXboxDispatchOwner, mInstance);
    g_exec_stack.start((F3DGfx*)commands);''')
    return _once(text, '        gfx_step();\n    }\n\n    Flush();',
        '        gfx_step();\n    }\n    }\n\n    Flush();')


GFX_PATH = '''// Only the debugger reads gfx_path (HasBreakPoint); Run records it while debugging.
static __declspec(thread) bool sXboxTrackGfxPath = true;
struct XboxGfxPathScope {
    bool previous;
    explicit XboxGfxPathScope(bool enabled) : previous(sXboxTrackGfxPath) { sXboxTrackGfxPath = enabled; }
    ~XboxGfxPathScope() { sXboxTrackGfxPath = previous; }
};

'''


def debugger_snapshot(text):
    """Read the debugger once per Run; record gfx_path only while debugging.

    Debugging starts and stops through the GUI between Runs, and a breakpoint
    ends the walk. Pops already tolerate an empty path; other walks keep the
    upstream path policy.
    """
    text = _once(text, 'void GfxExecStack::start(F3DGfx* dlist) {', GFX_PATH + 'void GfxExecStack::start(F3DGfx* dlist) {')
    if text.count('    gfx_path.push_back(caller);') != 2:
        raise ValueError('Fast3D dispatch contract changed: gfx_path writers')
    text = text.replace('    gfx_path.push_back(caller);', '    if (sXboxTrackGfxPath) gfx_path.push_back(caller);')
    start = text.index('void Interpreter::Run(')
    end = text.index('\n}', start)
    body = text[start:end]
    match = re.findall(r'\n(    auto dbg = [^\n]*;)\n', body)
    if len(match) != 1:
        raise ValueError('Fast3D dispatch contract changed: debugger handle')
    head, tail = body.split(match[0])
    _once(tail, '        if (dbg->IsDebugging()) {', '')
    # Every later read in Run (e.g. fast3d_triangle_run's enable) sees the snapshot.
    body = head + match[0] + '\n    const bool xboxDebugging = dbg->IsDebugging();' + tail.replace(
        'dbg->IsDebugging()', 'xboxDebugging')
    body = _once(body, '    g_exec_stack.start((F3DGfx*)commands);',
                 '    XboxGfxPathScope gfxPathScope(xboxDebugging);\n    g_exec_stack.start((F3DGfx*)commands);')
    return text[:start] + body + text[end:]
