"""Render the fixed-arity variant used by the XDK compatibility headers.

The XDK compiler needs explicit alternatives and switch arms. This module owns
that repetition; sources/value_types/xbox_compat/variant.h is generated.
"""

MAX = 19


def _names():
    return [f'T{index}' for index in range(MAX)]


def _indent(text, spaces):
    return '\n'.join(' ' * spaces + line for line in text.splitlines())


def _maximum(names, value):
    result = value(names[0])
    for name in names[1:]:
        result = ('value_detail::maximum<\n' + _indent(result, 2) + ',\n'
                  '  ' + value(name) + '\n>::value')
    return result


def _cases(names, operation):
    lines = []
    for index, name in enumerate(names):
        lines += [
            f'        case {index}:',
            f'            {operation(index, name)}',
            '            break;',
        ]
    return lines


def _storage_and_selection(names):
    sizes = _maximum(names, lambda name: f'sizeof({name})')
    alignments = _maximum(
        names, lambda name: f'std::alignment_of<{name}>::value')
    unique = ' &&\n        '.join(
        f'!std::is_same<{left},{right}>::value'
        for index, left in enumerate(names)
        for right in names[index + 1:]
    )
    lines = [
        '    enum {',
        '        storage_size =\n' + _indent(sizes, 12) + ',',
        '        storage_alignment =\n' + _indent(alignments, 12),
        '    };',
        '    typedef typename std::aligned_storage<storage_size,storage_alignment>::type storage_type;',
        '    typedef char alignment_check[(std::alignment_of<storage_type>::value >= storage_alignment)?1:-1];',
        '    storage_type storage_;',
        '    std::size_t index_;',
        '    typedef char unique_alternatives[\n        ' + unique +
        '\n        ?1:-1];',
    ]
    for index, name in enumerate(names):
        lines += [
            '    template<class U>',
            '    static typename std::enable_if<',
            f'        std::is_convertible<U,{name}>::value &&',
            f'        value_detail::nonnarrowing<U,{name}>::value,',
            f'        value_detail::choice<{index},{name}> >::type select({name});',
        ]
    return lines


def _private_members(names):
    lines = _storage_and_selection(names)
    lines += [
        '    template<class U> struct selection {',
        '        typedef decltype(variant::template select<U>(value_detail::declval<U>())) type;',
        '    };',
        '    template<class T> T* ptr() { return reinterpret_cast<T*>(&storage_); }',
        '    template<class T> const T* ptr() const { return reinterpret_cast<const T*>(&storage_); }',
        '    template<class T,class U> void construct(U&& value,std::size_t index) {',
        '        ::new(static_cast<void*>(&storage_)) T(value_detail::forward<U>(value));',
        '        index_=index;',
        '    }',
        '    void destroy() {',
        '        std::size_t old=index_;',
        '        index_=variant_npos;',
        '        switch(old) {',
    ]
    lines += _cases(names, lambda _index, name: f'ptr<{name}>()->~{name}();')
    lines += [
        '        default: break;',
        '        }',
        '    }',
        '    template<class T> void copy_assign_alternative(const variant& other,std::size_t index) {',
        '        if(index_==index) {',
        '            *ptr<T>()=*other.ptr<T>();',
        '            return;',
        '        }',
        '        if(is_nothrow_copy<T>::value || !is_nothrow_move<T>::value) {',
        '            destroy();',
        '            construct<T>(*other.ptr<T>(),index);',
        '        } else {',
        '            T temporary(*other.ptr<T>());',
        '            destroy();',
        '            construct<T>(value_detail::move(temporary),index);',
        '        }',
        '    }',
        '    template<class T,class U> void assign_converted(U&& value,std::size_t index) {',
        '        if(index_==index) {',
        '            *ptr<T>()=value_detail::forward<U>(value);',
        '            return;',
        '        }',
        '        if(value_detail::nothrow_conversion<T,U>::value || !is_nothrow_move<T>::value) {',
        '            destroy();',
        '            construct<T>(value_detail::forward<U>(value),index);',
        '        } else {',
        '            T temporary(value_detail::forward<U>(value));',
        '            destroy();',
        '            construct<T>(value_detail::move(temporary),index);',
        '        }',
        '    }',
    ]
    return lines


def _constructors(names, viable):
    lines = [
        '    variant():index_(variant_npos) {',
        '        ::new(static_cast<void*>(&storage_)) T0();',
        '        index_=0;',
        '    }',
        '    variant(const variant& other):index_(variant_npos) {',
        '        switch(other.index_) {',
    ]
    lines += _cases(
        names, lambda index, name: f'construct<{name}>(*other.ptr<{name}>(),{index});')
    lines += [
        '        default: break;',
        '        }',
        '    }',
        '    variant(variant&& other):index_(variant_npos) {',
        '        switch(other.index_) {',
    ]
    lines += _cases(
        names,
        lambda index, name: (
            f'construct<{name}>(value_detail::move(*other.ptr<{name}>()),{index});'
        ),
    )
    lines += [
        '        default: break;',
        '        }',
        '    }',
        '    template<class U> variant(',
        '        U&& value,',
        '        typename std::enable_if<',
        '            !std::is_same<typename value_detail::decay<U>::type,variant>::value && (',
        f'            {viable}',
        '            ),int>::type = 0',
        '    ):index_(variant_npos) {',
        '        typedef typename selection<U>::type selected;',
        '        construct<typename selected::type>(value_detail::forward<U>(value),selected::index);',
        '    }',
        '    ~variant() { destroy(); }',
    ]
    return lines


def _assignments(names):
    lines = [
        '    variant& operator=(const variant& other) {',
        '        if(this==&other) return *this;',
        '        switch(other.index_) {',
    ]
    lines += _cases(
        names, lambda index, name: f'copy_assign_alternative<{name}>(other,{index});')
    lines += [
        '        default:',
        '            destroy();',
        '            break;',
        '        }',
        '        return *this;',
        '    }',
        '    variant& operator=(variant&& other) {',
        '        if(this==&other) return *this;',
        '        switch(other.index_) {',
    ]
    for index, name in enumerate(names):
        lines += [
            f'        case {index}:',
            f'            if(index_=={index})',
            f'                *ptr<{name}>()=value_detail::move(*other.ptr<{name}>());',
            '            else {',
            '                destroy();',
            f'                construct<{name}>(value_detail::move(*other.ptr<{name}>()),{index});',
            '            }',
            '            break;',
        ]
    lines += [
        '        default:',
        '            destroy();',
        '            break;',
        '        }',
        '        return *this;',
        '    }',
        '    template<class U>',
        '    typename std::enable_if<',
        '        !std::is_same<typename value_detail::decay<U>::type,variant>::value,',
        '        variant&>::type operator=(U&& value) {',
        '        typedef typename selection<U>::type selected;',
        '        assign_converted<typename selected::type>('
        'value_detail::forward<U>(value),selected::index);',
        '        return *this;',
        '    }',
    ]
    return lines


def _index_of(names):
    index_of = '\n        '.join(
        f'std::is_same<U,{name}>::value ? {index} :'
        for index, name in enumerate(names)
    ) + '\n        -1'
    return ('    template<class U> struct index_of {\n'
            '        enum { value =\n        ' + index_of + ' };\n'
            '    };')


def _accessors():
    return [
        '    std::size_t index() const { return index_; }',
        '    bool valueless_by_exception() const { return index_==variant_npos; }',
        '    template<class U> U& access() {',
        '        typedef char present[index_of<U>::value>=0?1:-1];',
        '        if(index_!=static_cast<std::size_t>(index_of<U>::value))',
        '            throw bad_variant_access();',
        '        return *ptr<U>();',
        '    }',
        '    template<class U> const U& access() const {',
        '        typedef char present[index_of<U>::value>=0?1:-1];',
        '        if(index_!=static_cast<std::size_t>(index_of<U>::value))',
        '            throw bad_variant_access();',
        '        return *ptr<U>();',
        '    }',
    ]


def _free_functions(names):
    parameters = ',\n    '.join('class ' + name for name in names)
    arguments = ',\n    '.join(names)
    lines = []
    overloads = (
        ('&', 'U&', 'value.template access<U>()'),
        ('const&', 'const U&', 'value.template access<U>()'),
        ('&&', 'U&&', 'value_detail::move(value.template access<U>())'),
        ('const&&', 'const U&&', 'value_detail::move(value.template access<U>())'),
    )
    for qualifier, result, expression in overloads:
        cv = 'const ' if qualifier.startswith('const') else ''
        reference = qualifier.replace('const', '')
        lines += [
            'template<\n    class U,\n    ' + parameters + '\n>',
            f'{result} get({cv}variant<\n    {arguments}\n>{reference} value) {{',
            f'    return {expression};',
            '}',
        ]
    lines += [
        'template<\n    class U,\n    ' + parameters + '\n>',
        f'bool holds_alternative(const variant<\n    {arguments}\n>& value) {{',
        f'    typedef variant<\n    {arguments}\n> V;',
        '    typedef char present[V::template index_of<U>::value>=0?1:-1];',
        '    return value.index()==static_cast<std::size_t>('
        'V::template index_of<U>::value);',
        '}',
    ]
    for cv in ('', 'const '):
        lines += [
            'template<\n    class U,\n    ' + parameters + '\n>',
            f'{cv}U* get_if({cv}variant<\n    {arguments}\n>* value) throw() {{',
            '    return value && holds_alternative<U>(*value)',
            '        ? std::addressof(value->template access<U>()) : 0;',
            '}',
        ]
    return lines


def variant_header():
    names = _names()
    defaults = ',\n    '.join(
        'class ' + name +
        (f' = value_detail::absent<{index}>' if index else '')
        for index, name in enumerate(names)
    )
    # The XDK does not reliably discard the constructor when decltype(select)
    # is used directly as a default parameter. Gate it with the same predicates.
    viable = ' ||\n            '.join(
        f'(std::is_convertible<U,{name}>::value && '
        f'value_detail::nonnarrowing<U,{name}>::value)'
        for name in names
    )
    lines = [
        '// Generated by xbox360_platform.value_types. Do not edit.',
        '#ifndef XBOX_COMPAT_VARIANT_H',
        '#define XBOX_COMPAT_VARIANT_H',
        '#include "common.h"',
        'namespace xbox_compat {',
        f'template<\n    {defaults}\n> class variant {{',
    ]
    lines += _private_members(names)
    lines.append('public:')
    lines.append(_index_of(names))
    lines += _constructors(names, viable)
    lines += _assignments(names)
    lines += _accessors()
    lines.append('};')
    lines += _free_functions(names)
    lines += ['}', '#endif', '']
    return '\n'.join(lines)
