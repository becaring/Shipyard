"""Borrow lookup names and map-owned CVars only within each getter call."""


def header(text):
    # Newer LUS adds transparent std::string_view hashers; VC10 cannot use them.
    anchors = [a for a in ('std::unordered_map<std::string, std::shared_ptr<CVar>> mVariables;',
                           'std::unordered_map<std::string, std::shared_ptr<CVar>, TransparentStringHash, '
                           'TransparentStringEqual> mVariables;') if a in text]
    if len(anchors) != 1 or text.count(anchors[0]) != 1:
        raise ValueError('CVar key container drift')
    anchor = anchors[0]
    return '#include "config/console_variable_key.h"\n' + text.replace(anchor,
        'std::unordered_map<Shipyard::ConsoleVariables::NameKey, std::shared_ptr<CVar>, '
        'Shipyard::ConsoleVariables::NameHash> mVariables;')


def transform(text):
    for name in ('GetInteger', 'GetFloat', 'GetString', 'GetColor', 'GetColor24'):
        signature = 'ConsoleVariable::' + name + '('
        if text.count(signature) != 1:
            raise ValueError('CVar getter signature drift: ' + name)
        start = text.index(signature)
        end = text.index('\n}', start) + 2
        body = text[start:end]
        anchor = '    auto variable = Get(name);'
        if body.count(anchor) != 1:
            raise ValueError('CVar getter lookup drift: ' + name)
        # The map retains ownership and these getters only inspect/copy values.
        # Nothing survives this invocation except the borrowed string
        # result; public Get still returns shared ownership to its callers.
        body = body.replace(anchor,
            '    auto found = mVariables.find(name);\n'
            '    const CVar* variable = found != mVariables.end() ? found->second.get() : nullptr;')
        text = text[:start] + body + text[end:]
    # The only original find is public Get; five typed methods above add one
    # apiece. Setters, CopyVariable and erasure retain owning conversions.
    anchor = 'mVariables.find(name)'
    if text.count(anchor) != 6:
        raise ValueError('CVar find inventory drift')
    return text.replace(anchor,
        'mVariables.find(Shipyard::ConsoleVariables::NameKey('
        'name, Shipyard::ConsoleVariables::NameKey::Borrow))')
