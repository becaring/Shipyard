"""Project an interpreter's directional-light cache through checked source anchors."""


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Shade cache source anchor changed: " + old[:80])
    return text.replace(old, new, 1)


def header(text):
    text = _once(text, '    int mInterpolationIndexTarget;',
                 '    int mInterpolationIndexTarget;\n    Shipyard::Fast3D::ShadeCache xboxShadeCache;')
    return '#include "fast3d/shade_cache.h"\n' + text


def transform(text, color_accessor_declaration, color_accessor_type):
    start = text.index('void Interpreter::GfxSpVertex(')
    end = text.index('void Interpreter::GfxSpModifyVertex(', start)
    original = text[start:end]
    vertex = _once(original, '    uint8_t xboxShadeSources[MAX_VERTICES];', '''    const bool xboxCrossShade=xboxBatchColors && xboxNearestNormal &&
        !(mRsp->geometry_mode&G_LIGHTING_POSITIONAL) && (mRsp->geometry_mode&G_TEXTURE_GEN);
    bool xboxShadeSelected=false,xboxShadeReady=false;
    uint8_t xboxShadeSources[MAX_VERTICES];''')
    vertex = _once(vertex, '        const F3DVtx_t* v = &vertices[i].v;', '''        bool xboxStoreShade=false;
        uint32_t xboxStoreNormal=0;
        const F3DVtx_t* v = &vertices[i].v;''')
    refresh_start = vertex.index('            if (mRsp->lights_changed) {')
    refresh_end = vertex.index('            int r = ', refresh_start)
    refresh = vertex[refresh_start:refresh_end]
    marker = '        if (mRsp->geometry_mode & G_LIGHTING) {\n'
    vertex = _once(vertex, marker, marker + '''            // Refresh source lighting before consulting a prior command's cached result.
            if (xboxCrossShade && !xboxShadeSelected) {
''' + refresh + '''                xboxShadeSelected=true;
                xboxShadeReady=Shipyard::Fast3D::ShadeStateFinite(mRsp->current_num_lights,
                    mRsp->current_lights_coeffs,mRsp->current_lookat_coeffs);
                if(xboxShadeReady) {
                    xboxShadeCache.Initialize();
                    Shipyard::Fast3D::ShadeKey key;
                    key.Set(mRsp->current_num_lights,mRsp->current_lights,
                        ''' + color_accessor_type + '''(),mRsp->current_lights_coeffs,
                        (mRsp->geometry_mode&G_TEXTURE_GEN_LINEAR)?2:1,mRsp->current_lookat_coeffs,
                        mRsp->texture_scaling_factor.s,mRsp->texture_scaling_factor.t);
                    xboxShadeCache.Select(key);
                }
            }
''')
    marker = '''            } else {
            if (mRsp->lights_changed) {'''
    vertex = _once(vertex, marker, '''            } else {
            const uint32_t xboxNormalKey=xboxShadeReady?Shipyard::Fast3D::ShadeCache::Normal(vn->n):0;
            const Shipyard::Fast3D::ShadeValue* xboxCachedShade=xboxShadeReady?xboxShadeCache.Find(xboxNormalKey):nullptr;
            if(xboxCachedShade) {
                d->color.r=xboxCachedShade->rgb[0];d->color.g=xboxCachedShade->rgb[1];d->color.b=xboxCachedShade->rgb[2];
                U=(short)xboxCachedShade->u;V=(short)xboxCachedShade->v;
            } else {
            if (mRsp->lights_changed) {''')
    vertex = _once(vertex, '            } // Original unique-normal / positional-light path.', '''                if(xboxShadeReady) {
                    xboxStoreShade=true;xboxStoreNormal=xboxNormalKey;
                }
            } // Cache-miss shading/texgen path.
            } // Unique-normal / positional-light path.''')
    vertex = _once(vertex, '''            d->color.a = v->cn[3];
        }
    }''', '''            d->color.a = v->cn[3];
        }
        // Cache only the fully published vertex attributes.
        if(xboxStoreShade) {
            Shipyard::Fast3D::ShadeValue value;
            value.rgb[0]=d->color.r;value.rgb[1]=d->color.g;value.rgb[2]=d->color.b;
            value.u=d->u;value.v=d->v;
            xboxShadeCache.Store(xboxStoreNormal,value);
        }
    }''')
    return ('#include "fast3d/shade_cache.h"\n' + color_accessor_declaration + '\n' +
            _once(text, original, vertex))
