#include "fast3d/math.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>

int main() {
    const float cases[] = { -0.0f, 0.0f, -0.1f, 0.1f, -0.5f, 0.5f,
        -1.5f, 1.5f, 8388607.0f, 8388608.0f, 16777215.0f,
        -8388607.0f, std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity() };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        float expected = std::round(cases[i]);
        float result = Shipyard::Fast3D::Round(cases[i]);
        if (memcmp(&expected, &result, sizeof(float))) return 1;
    }
    for (int i = -65536; i <= 65536; ++i) {
        const float value = i / 8.0f;
        if (Shipyard::Fast3D::Round(value) != std::round(value)) return 2;
    }
    if (!std::isnan(Shipyard::Fast3D::Round(std::numeric_limits<float>::quiet_NaN()))) return 3;
    const double wideCases[] = { -0.0, 0.0, -0.5, 0.5, -1.5, 1.5,
        4503599627370495.0, 4503599627370496.0, 9007199254740991.0,
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
    for (unsigned i = 0; i < sizeof(wideCases)/sizeof(wideCases[0]); ++i) {
        double expected = std::round(wideCases[i]);
        double result = Shipyard::Fast3D::Round(wideCases[i]);
        if (memcmp(&expected, &result, sizeof(double))) return 4;
    }
    if (!std::isnan(Shipyard::Fast3D::Round(std::numeric_limits<double>::quiet_NaN()))) return 5;
    puts("PASS round boundary cases");
    return 0;
}
