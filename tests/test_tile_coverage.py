import math
import os
import random
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def clipped_to_view(triangle):
    polygon = triangle
    # D3D clip volume. Each vertex is homogeneous (x, y, z, w).
    planes = (
        lambda v: v[3] + v[0], lambda v: v[3] - v[0],
        lambda v: v[3] + v[1], lambda v: v[3] - v[1],
        lambda v: v[2], lambda v: v[3] - v[2],
    )
    for plane in planes:
        if not polygon:
            break
        output = []
        previous = polygon[-1]
        previous_distance = plane(previous)
        for current in polygon:
            current_distance = plane(current)
            if (previous_distance >= 0) != (current_distance >= 0):
                t = previous_distance / (previous_distance - current_distance)
                output.append(tuple(a + t * (b - a) for a, b in zip(previous, current)))
            if current_distance >= 0:
                output.append(current)
            previous, previous_distance = current, current_distance
        polygon = output
    return polygon


def touches_first_tile(vertices, viewport_y, viewport_height, split_y):
    for offset in range(0, len(vertices), 3):
        for vertex in clipped_to_view(vertices[offset:offset + 3]):
            screen_y = viewport_y + (1.0 - vertex[1] / vertex[3]) * viewport_height / 2.0
            if screen_y <= split_y:
                return True
    return False


class TileCoverageTests(unittest.TestCase):
    def test_classifier_against_clipped_triangle_oracle(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        driver = r'''
#include <iostream>
#include <vector>
#include <limits>
#include "tile_coverage.h"
int main() {
    const float zero[4] = {0, 0, 0, 0};
    const float nan[4] = {0, std::numeric_limits<float>::quiet_NaN(), 0, 1};
    const float inf[4] = {0, std::numeric_limits<float>::infinity(), 0, 1};
    const float nanX[4] = {std::numeric_limits<float>::quiet_NaN(), -1, 0, 1};
    const float nanZ[4] = {0, -1, std::numeric_limits<float>::quiet_NaN(), 1};
    if (Fast::ShipyardXenos::BatchInsideSecondTile(zero, 1, 4, 0, 1080, 640) ||
        Fast::ShipyardXenos::BatchInsideSecondTile(nan, 1, 4, 0, 1080, 640) ||
        Fast::ShipyardXenos::BatchInsideSecondTile(inf, 1, 4, 0, 1080, 640) ||
        Fast::ShipyardXenos::BatchInsideSecondTile(nanX, 1, 4, 0, 1080, 640) ||
        Fast::ShipyardXenos::BatchInsideSecondTile(nanZ, 1, 4, 0, 1080, 640)) return 2;
    unsigned cases; std::cin >> cases;
    for (unsigned c = 0; c < cases; ++c) {
        unsigned viewportY, viewportHeight, splitY, vertices, stride;
        std::cin >> viewportY >> viewportHeight >> splitY >> vertices >> stride;
        std::vector<float> data(vertices * stride);
        for (size_t i = 0; i < data.size(); ++i) std::cin >> data[i];
        std::cout << Fast::ShipyardXenos::BatchInsideSecondTile(
            &data[0], vertices, stride, viewportY, viewportHeight, splitY) << '\n';
    }
}
'''
        rng = random.Random(360)
        cases = []
        for index in range(4000):
            viewport_y = rng.choice((0, 16, 48))
            viewport_height = rng.choice((720, 900, 1080))
            split_y = viewport_y + rng.randrange(1, viewport_height)
            stride = rng.choice((4, 8, 13))
            below = index % 3 == 0
            vertices = []
            for _ in range(3 * rng.randint(1, 4)):
                w = f32(rng.uniform(0.1, 4.0))
                x = f32(rng.uniform(-1.5, 1.5) * w)
                # Concentrate some triangles around the tile split.
                y_pixels = split_y + (rng.uniform(3.0, 200.0) if below else rng.uniform(-30.0, 30.0))
                y = f32((1.0 - 2.0 * (y_pixels - viewport_y) / viewport_height) * w)
                vertices.append((x, y, f32(0.5 * w), w))
            cases.append((viewport_y, viewport_height, split_y, stride, vertices))
        with tempfile.TemporaryDirectory(prefix='shipyard-tile-coverage-') as folder:
            out = Path(folder)
            (out / 'driver.cpp').write_text(driver)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                             'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                             str(ROOT / 'sources/xenos') + '" driver.cpp /Fedriver.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            lines = [str(len(cases))]
            for viewport_y, viewport_height, split_y, stride, vertices in cases:
                lines.append(f'{viewport_y} {viewport_height} {split_y} {len(vertices)} {stride}')
                lines.append(' '.join(str(value) for vertex in vertices
                                      for value in (*vertex, *((0.0,) * (stride - 4)))))
            run = subprocess.run([str(out / 'driver.exe')], cwd=out, input='\n'.join(lines) + '\n',
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            results = [int(value) for value in run.stdout.split()]
            self.assertEqual(len(results), len(cases))
            self.assertGreater(sum(results), 100)
            for result, case in zip(results, cases):
                if result:
                    self.assertFalse(touches_first_tile(case[4], case[0], case[1], case[2]))

    def test_probe_is_profile_only_unless_trial_is_enabled(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        draw = function(source, 'void Draw(float* vertices, size_t count, size_t triangles) {')
        self.assertLess(draw.index('if (timing_enabled || tile1_predication_trial_enabled)'),
                        draw.index('BatchInsideSecondTile('))
        self.assertLess(draw.index('BatchInsideSecondTile('), draw.index('SubmitTile1PredicatedDraw('))
        self.assertNotIn('SetPredication(', draw)


if __name__ == '__main__':
    unittest.main()
