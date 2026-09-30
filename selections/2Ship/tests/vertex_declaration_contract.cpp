#include <cstdio>
#include <vector>

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
// Xbox SDK d3d9types.h: Type is DWORD, unlike the desktop D3D9 BYTE field.
struct D3DVERTEXELEMENT9 {
    WORD Stream;
    WORD Offset;
    DWORD Type;
    BYTE Method;
    BYTE Usage;
    BYTE UsageIndex;
};
// XDK MAKED3DDECLTYPE values: format, numeric interpretation, and swizzle.
const DWORD D3DDECLTYPE_FLOAT1 = 0x002c83a4;
const DWORD D3DDECLTYPE_FLOAT2 = 0x002c23a5;
const DWORD D3DDECLTYPE_FLOAT3 = 0x002a23b9;
const DWORD D3DDECLTYPE_FLOAT4 = 0x001a23a6;
const BYTE D3DDECLMETHOD_DEFAULT = 0;
#include "element.inc"

int main() {
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 12;
    const WORD offsets[] = {12, 16, 24, 36};
    // The hardware's three-bit component selectors: X=0,Y=1,Z=2,W=3,0=4,1=5.
    const unsigned swizzles[4][4] = {{0,4,4,5}, {0,1,4,5}, {0,1,2,5}, {0,1,2,3}};
    const float input[] = {2, 3, 5, 7, 0, 1};
    const float expected[4][4] = {{2,0,0,1}, {2,3,0,1}, {2,3,5,1}, {2,3,5,7}};
    for (int count = 1; count <= 4; ++count) {
        Element(elements, offset, count, 5, (BYTE)(count - 1));
        const D3DVERTEXELEMENT9& e = elements.back();
        if (e.Stream != 0 || e.Offset != offsets[count-1] || e.Method != 0 ||
            e.Usage != 5 || e.UsageIndex != count-1) return 1;
        if ((e.Type & 0x300) != 0x300) {
            std::fprintf(stderr, "FLOAT%d lost signed/float interpretation: %08x\n", count, e.Type);
            return 2;
        }
        for (int component = 0; component < 4; ++component) {
            const unsigned selector = (e.Type >> (10 + 3 * component)) & 7;
            if (selector != swizzles[count-1][component] ||
                input[selector] != expected[count-1][component]) return 3;
        }
    }
    if (offset != 52 || elements.size() != 4) return 4;
    std::puts("native vertex formats PASS");
    return 0;
}
