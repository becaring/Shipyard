"""Split the common combiner lookup path without owning either consumer's key or map."""

DECLARATION = '    ColorCombiner* LookupOrCreateColorCombiner(const ColorCombinerKey& key);'
SIGNATURE = 'ColorCombiner* Interpreter::LookupOrCreateColorCombiner(const ColorCombinerKey& key) {'
BOUNDARY = '''    mPrevCombiner = mColorCombinerPool.find(key);
    if (mPrevCombiner != mColorCombinerPool.end()) {'''


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('combiner lookup source anchor changed')
    return text.replace(old, new, 1)


def header(text):
    return _once(text, DECLARATION,
                 DECLARATION + '\n    ColorCombiner* XboxLookupColorCombinerCold(const ColorCombinerKey& key);')


def transform(text):
    text = _once(text, BOUNDARY,
                 '    return XboxLookupColorCombinerCold(key);\n}\n\n'
                 '__declspec(noinline) ColorCombiner* Interpreter::XboxLookupColorCombinerCold('
                 'const ColorCombinerKey& key) {\n' + BOUNDARY)
    return _once(text, SIGNATURE, '__forceinline ' + SIGNATURE)
