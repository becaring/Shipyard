// Xenon compile check: the triangle-row and deferred-vertex templates
// instantiate with consumer-shaped types under the XDK compiler.
#include "fast3d/row_reuse.h"
#include "fast3d/deferred_vertex.h"
#include "fast3d/aspect_reciprocal.h"
#include "fast3d/coordinate_three.h"

typedef struct { short ob[3]; unsigned short flag; short tc[2]; unsigned char cn[4]; } F3DVtx_t;
typedef struct { short ob[3]; unsigned short flag; short tc[2]; signed char n[3]; unsigned char a; } F3DVtx_tn;
typedef union { F3DVtx_t v; F3DVtx_tn n; long long align; } F3DVtx;
struct RGBA { uint8_t r, g, b, a; };
struct LoadedVertex { float x, y, z, w, u, v; RGBA color; uint8_t clip_rej; };
struct F3DLight { struct { uint8_t col[3]; uint8_t pad; int8_t dir[3]; } l; };

static Shipyard::Fast3D::TriangleRows<256> rows;
static Shipyard::Fast3D::DeferredVertices<F3DVtx, 64, 32> deferred;

unsigned Plan(LoadedVertex* corners[3], LoadedVertex* pack[3], size_t triangle, float* x, float* y) {
    rows.Begin(true);
    rows.ndc.Load(3, *corners[0], *x, *y);
    const unsigned count = rows.Plan(corners, pack, triangle, true);
    rows.End();
    return count + (rows.Indexed(triangle + 1) ? 1 : 0);
}

bool Defer(const F3DLight* lights, unsigned count, const float (*coeffs)[3], const F3DVtx* vertices,
           LoadedVertex* loaded) {
    Shipyard::Fast3D::DeferredLighting<32>* context =
        deferred.Snapshot(lights, count, coeffs, true, 1024, 2048, 300, 12, true);
    if (!context)
        return false;
    deferred.Discard(4, 8);
    deferred.Commit(vertices, 8, 4, context);
    deferred.Materialize(5, loaded);
    deferred.MaterializeAll(loaded);
    return deferred.Waiting(6);
}

float Aspect(float x, float denominator) {
    const Shipyard::Fast3D::AspectReciprocal aspect(denominator);
    return aspect.Divide(x * (4.0f / 3.0f), denominator);
}
