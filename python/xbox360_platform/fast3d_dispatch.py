"""Pin one interpreter owner while a version-locked command walk dispatches."""


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
