"""Native differential driver for a consumer's real combiner lookup method."""
from pathlib import Path
import subprocess


SIGNATURE = 'ColorCombiner* Interpreter::LookupOrCreateColorCombiner(const ColorCombinerKey& key) {'
COLD_SIGNATURE = 'ColorCombiner* Interpreter::XboxLookupColorCombinerCold(const ColorCombinerKey& key) {'


def method(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]


def key_struct(header):
    start = header.index('struct ColorCombinerKey {')
    end = header.index('\n};', start) + 3
    return header[start:end]


CLASS = '''namespace __NAME__ {
struct Interpreter {
    std::map<ColorCombinerKey, ColorCombiner> mColorCombinerPool;
    std::map<ColorCombinerKey, ColorCombiner>::iterator mPrevCombiner = mColorCombinerPool.end();
    int flushes = 0;
    int generations = 0;
    void Flush() { ++flushes; }
    void GenerateCC(ColorCombiner* comb, const ColorCombinerKey&) { comb->generation = ++generations; }
    void Reset() { mColorCombinerPool.clear(); mPrevCombiner = mColorCombinerPool.end(); }
    ColorCombiner* LookupOrCreateColorCombiner(const ColorCombinerKey& key);
    ColorCombiner* XboxLookupColorCombinerCold(const ColorCombinerKey& key);
};
__METHODS__
}
'''

DRIVER = '''#include <compare>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
__KEY__
struct ColorCombiner { int generation = 0; };
__ORIGINAL_CLASS__
__PROJECTED_CLASS__
template<class T> std::vector<int> scenario() {
    T interpreter;
    ColorCombinerKey a = __KEY_A__;
    ColorCombinerKey b = __KEY_B__;
    ColorCombinerKey c = __KEY_C__;
    ColorCombinerKey d = __KEY_D__;
    std::vector<int> result;
    auto first = interpreter.LookupOrCreateColorCombiner(a);
    result.push_back(first->generation);
    auto repeat = interpreter.LookupOrCreateColorCombiner(a);
    if (first != repeat) throw std::runtime_error("warm hit changed address");
    result.push_back(repeat->generation);
    result.push_back(interpreter.LookupOrCreateColorCombiner(b)->generation);
    auto prior = interpreter.LookupOrCreateColorCombiner(a);
    if (prior != first) throw std::runtime_error("map hit changed address");
    result.push_back(prior->generation);
    result.push_back(interpreter.LookupOrCreateColorCombiner(a)->generation);
    result.push_back(interpreter.flushes);
    result.push_back(interpreter.generations);
    interpreter.Reset();
    result.push_back(interpreter.LookupOrCreateColorCombiner(a)->generation);
    result.push_back(interpreter.LookupOrCreateColorCombiner(c)->generation);
    result.push_back(interpreter.LookupOrCreateColorCombiner(d)->generation);
    result.push_back(interpreter.flushes);
    result.push_back(interpreter.generations);
    result.push_back(static_cast<int>(interpreter.mColorCombinerPool.size()));
    return result;
}
int main() {
    const auto original = scenario<Original::Interpreter>();
    const auto projected = scenario<Projected::Interpreter>();
    const std::vector<int> expected = {1,1,2,1,1,2,2,3,4,5,5,5,3};
    if (original != expected || projected != expected || original != projected) return 1;
    std::cout << "combiner lookup differential: 13 observations matched\\n";
}
'''


def exercise(test, source, header, projected, output, keys):
    original = CLASS.replace('__NAME__', 'Original').replace('__METHODS__', method(source, SIGNATURE))
    warm_cold = method(projected, SIGNATURE) + '\n' + method(projected, COLD_SIGNATURE)
    transformed = CLASS.replace('__NAME__', 'Projected').replace('__METHODS__', warm_cold)
    driver = DRIVER
    for old, new in [('__KEY__', key_struct(header)),
                     ('__ORIGINAL_CLASS__', original), ('__PROJECTED_CLASS__', transformed),
                     ('__KEY_A__', keys[0]), ('__KEY_B__', keys[1]),
                     ('__KEY_C__', keys[2]), ('__KEY_D__', keys[3])]:
        driver = driver.replace(old, new)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'contract.cpp').write_text(driver)
    (output / 'build.cmd').write_text('@echo off\ncall "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                                      'cl /nologo /std:c++20 /O2 /EHsc /MT contract.cpp /Fe:contract.exe\nexit /b %errorlevel%\n')
    build = subprocess.run(['cmd', '/d', '/c', str(output / 'build.cmd')], cwd=output, capture_output=True, text=True)
    (output / 'compile.log').write_text(build.stdout + build.stderr)
    test.assertEqual(build.returncode, 0, build.stdout + build.stderr)
    run = subprocess.run([str(output / 'contract.exe')], cwd=output, capture_output=True, text=True)
    (output / 'run.log').write_text(run.stdout + run.stderr)
    test.assertEqual(run.returncode, 0, run.stdout + run.stderr)
    test.assertIn('13 observations matched', run.stdout)
