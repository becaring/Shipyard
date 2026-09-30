"""Separate Fast3D triangle rejection from its state and emission body."""

SIGNATURE = 'void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {'
NEXT_METHOD = '\nvoid Interpreter::GfxSpGeometryMode('
PREAMBLE = '''    struct LoadedVertex* v1 = &mRsp->loaded_vertices[vtx1_idx];
    struct LoadedVertex* v2 = &mRsp->loaded_vertices[vtx2_idx];
    struct LoadedVertex* v3 = &mRsp->loaded_vertices[vtx3_idx];
    struct LoadedVertex* v_arr[3] = { v1, v2, v3 };
'''
DECLARATION = '    void GfxSpTri1(uint8_t vtx1Idx, uint8_t vtx2Idx, uint8_t vtx3Idx, bool isRect);'


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Triangle split source anchor changed: ' + old[:80])
    return text.replace(old, new, 1)


def header(text, observer_type=None, observer_name=None):
    if bool(observer_type) != bool(observer_name):
        raise ValueError('Triangle observer type and name must be supplied together')
    observer = ', ' + observer_type + '& ' + observer_name if observer_type else ''
    declaration = ('    void XboxEmitTriangle(uint8_t vtx1_idx, uint8_t vtx2_idx, '
                   'uint8_t vtx3_idx, bool is_rect' + observer + ');')
    text = _once(text, DECLARATION, DECLARATION + '\n' + declaration)
    return ('struct ' + observer_type + ';\n' if observer_type else '') + text


def transform(text, boundary, observer_type=None, observer_name=None):
    if bool(observer_type) != bool(observer_name):
        raise ValueError('Triangle observer type and name must be supplied together')
    if text.count(SIGNATURE) != 1:
        raise ValueError('Triangle method inventory drift')
    start = text.index(SIGNATURE)
    end = text.index(NEXT_METHOD, start)
    triangle = text[start:end]
    if triangle.count(PREAMBLE) != 1 or triangle.count(boundary) != 1:
        raise ValueError('Triangle preamble or emission boundary drift')
    if observer_name and triangle.count(observer_type + ' ' + observer_name + ';') != 1:
        raise ValueError('Triangle observer owner drift')
    arguments = 'vtx1_idx, vtx2_idx, vtx3_idx, is_rect'
    parameters = 'uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect'
    if observer_name:
        arguments += ', ' + observer_name
        parameters += ', ' + observer_type + '& ' + observer_name
    emission = ('    XboxEmitTriangle(' + arguments + ');\n}\n\n'
                '__declspec(noinline) void Interpreter::XboxEmitTriangle(' + parameters + ') {\n'
                + PREAMBLE + '\n' + boundary)
    triangle = _once(triangle, boundary, emission)
    return text[:start] + triangle + text[end:]
