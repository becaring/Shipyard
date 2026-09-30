"""Checked libgfxd source adaptation for the legacy Xbox C compiler."""
import re

REVISION = '008f73dca8ebc9151b205959b17773a19c5bd0da'
SCHEMAS = {
    'gfxd_arg_type_t': ('fmt','fn'),
    'gfxd_macro_type_t': ('prefix','suffix','opcode','n_arg','n_gfx','disas_fn','combine_fn','alias','ext'),
    'struct gfxd_ucode': ('disas_fn','combine_fn','arg_tbl','macro_tbl'),
    'struct gfxd_config': ('ucode','endian','wordsize','arg','udata','stop_on_invalid','stop_on_end',
        'emit_dec_color','emit_q_macro','emit_ext_macro','input_buf','input_buf_size','input_fd','input_fn',
        'output_buf','output_buf_size','output_fd','output_fn','macro_fn','arg_fn','tlut_fn','timg_fn','cimg_fn',
        'zimg_fn','dl_fn','mtx_fn','lookat_fn','light_fn','seg_fn','vtx_fn','vp_fn','uctext_fn','ucdata_fn','dram_fn'),
}


def adapt(name, text, *, revision, gbi):
    if revision != REVISION:
        raise ValueError('Unreviewed libgfxd revision')
    if name in ('gfxd.c','uc.c'):
        if text.count('#include <inttypes.h>') != 1:
            raise ValueError('libgfxd integer formatting include changed')
        text = text.replace('#include <inttypes.h>','#include <gfxd/compat.h>')
    if name == 'gfxd.c':
        if text.count('vsnprintf(') != 1:
            raise ValueError('libgfxd formatting call changed')
        text = text.replace('vsnprintf(','Shipyard_Vsnprintf(')
    if name == 'gbi.h':
        if text.count('_Alignas(8)') != 12:
            raise ValueError('libgfxd alignment inventory changed')
        text = text.replace('_Alignas(8)','__declspec(align(8))')
    if name == 'priv.h':
        if text.count('#define UCFUNC static inline') != 1:
            raise ValueError('libgfxd microcode function declaration changed')
        text = text.replace('#define UCFUNC static inline','#define UCFUNC static __inline')
    if name == 'uc_argfn.c':
        definitions = dict(re.findall(r'^#define\s+(_?G_CC_\w+)\s+([^\n]+)',gbi.replace('\\\n',' '),re.M))
        count = 0
        def expand(match):
            nonlocal count
            name = match[1]; seen = set()
            while name in definitions:
                if name in seen:
                    raise ValueError('Cyclic combiner macro')
                seen.add(name); name = definitions[name].strip()
            fields = [n.strip() for n in name.split(',')]
            if len(fields) != 8 or any(not re.fullmatch(r'\w+',n) for n in fields):
                raise ValueError('Unreviewed combiner expression: '+name)
            count += 1
            return 'CC_('+', '.join(fields)+')'
        text = re.sub(r'\bCC\((_?G_CC_\w+)\)',expand,text)
        if count != 57:
            raise ValueError('Combiner macro inventory changed: '+str(count))
    # The pinned dependency uses binary literals in macro definitions as well as C expressions.
    tokens = re.findall(r'(?<!\w)0b[01]+(?:[uUlL]*)(?!\w)',text)
    for token in set(tokens):
        match = re.fullmatch(r'0b([01]+)([uUlL]*)',token)
        text = re.sub(r'(?<!\w)'+re.escape(token)+r'(?!\w)',hex(int(match[1],2))+match[2],text)
    if re.search(r'\b0b',text):
        raise ValueError('Unmatched binary integer form')
    return text


def enum_indices(header, parser, walk):
    result = {}; groups = []
    for node in walk(parser.parse(header).root_node):
        if node.type != 'enum_specifier':
            continue
        body = node.child_by_field_name('body')
        if body is None:
            continue
        members = [n for n in body.named_children if n.type == 'enumerator']
        if not members or members[0].child_by_field_name('name').text not in (b'gfxd_Word',b'gfxd_Invalid'):
            continue
        groups.append(len(members))
        for index,member in enumerate(members):
            name = member.child_by_field_name('name').text.decode()
            if member.child_by_field_name('value') is not None or name in result:
                raise ValueError('libgfxd enum identities changed')
            result[name] = index
    if groups != [96,139]:
        raise ValueError('libgfxd enum cardinalities changed: '+repr(groups))
    return result


def lower_preprocessed(source, unit, header, parser, walk):
    from .c_designated_tables import lower
    from .c89_lower import lower as lower_c89
    records = {'gfxd_config__':'struct gfxd_config'} if unit=='gfxd.c' else {'uc':'struct gfxd_ucode'}
    tables = {} if unit=='gfxd.c' else {'arg_tbl':'gfxd_arg_type_t','macro_tbl':'gfxd_macro_type_t'}
    source = lower(source,parser,walk,records=records,tables=tables,schemas=SCHEMAS,
        indices=enum_indices(header,parser,walk))
    source,changes = lower_c89(source,active_source=lambda p: b'/gfxd/' in p.lower().replace(b'\\',b'/'),xdk_inline=True)
    return source,changes
