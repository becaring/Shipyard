"""Project a guarded flattened-leaf fast path into the shared Config owner.

Replacing an existing primitive leaf preserves the certificate. Other writes
invalidate it; Save recertifies after its required unflatten. Reads never
mutate the guard, and malformed writes retain their deferred read failure.
"""


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Config leaf lookup source drift: ' + old[:80])
    return text.replace(old, new)


def project_header(text):
    return once(text, '    bool mIsNewInstance;',
                '    bool mIsNewInstance;\n    bool mFlatLeafSafe;')


def project_source(text):
    text = once(text, 'mIsNewInstance(false) {\n    mPath =',
                'mIsNewInstance(false), mFlatLeafSafe(false) {\n    mPath =')
    begin = text.index('nlohmann::json Config::Nested(const std::string& key) {')
    end = text.index('\nstd::string Config::GetString(', begin)
    text = text[:begin] + '''// A canonical flattened object has the same value at a simple leaf pointer.
// Other keys retain Nested's skip-missing, wildcard and array traversal.
static bool SimpleConfigFlatPointer(const std::string& key, std::string& pointer) {
    if (key.empty() || key[0] == '.' || key[key.size() - 1] == '.' ||
        key.find_first_of("*/~") != std::string::npos) return false;
    pointer.clear();
    pointer.reserve(key.size() + 1);
    pointer.push_back('/');
    size_t first = 0;
    for (size_t i = 0; i <= key.size(); ++i) {
        if (i != key.size() && key[i] != '.') continue;
        if (i == first) return false;
        bool numeric = true;
        for (size_t j = first; j < i; ++j) {
            if (key[j] < '0' || key[j] > '9') { numeric = false; break; }
        }
        if (numeric) return false;
        pointer.append(key, first, i - first);
        if (i != key.size()) pointer.push_back('/');
        first = i + 1;
    }
    return true;
}

static bool ConfigPreserveScalarWrite(const nlohmann::json& flat, const std::string& key) {
    std::string pointer;
    if (!SimpleConfigFlatPointer(key, pointer)) return false;
    nlohmann::json::const_iterator found = flat.find(pointer);
    return found != flat.end() && found->is_primitive();
}

static bool TryFlattenedConfigLeaf(const nlohmann::json& flat, const std::string& key, nlohmann::json& value) {
    std::string pointer;
    if (!SimpleConfigFlatPointer(key, pointer)) return false;
    nlohmann::json::const_iterator found = flat.find(pointer);
    if (found == flat.end()) return false;
    value = *found;
    return true;
}

nlohmann::json Config::Nested(const std::string& key) {
    if (!mFlattenedJson.is_object()) return mFlattenedJson;
    nlohmann::json leaf;
    if (mFlatLeafSafe && TryFlattenedConfigLeaf(mFlattenedJson, key, leaf)) return leaf;

    std::vector<std::string> dots = StringHelper::Split(key, ".");
    nlohmann::json gjson = mFlattenedJson.unflatten();
    if (dots.size() > 1) {
        for (size_t i = 0; i < dots.size(); ++i) {
            std::string& dot = dots[i];
            if (dot == "*" || gjson.contains(dot)) {
                gjson = gjson[dot];
            }
        }
        return gjson;
    }
    return gjson[key];
}
''' + text[end:]
    mutation = 'mFlattenedJson[FormatNestedKey(key)] ='
    if text.count(mutation) != 6:
        raise ValueError('Config scalar/array mutation count drift')
    text = text.replace('    ' + mutation + ' value;\n}',
                        '    const bool preserveFlatLeaf = mFlatLeafSafe && ConfigPreserveScalarWrite(mFlattenedJson, key);\n'
                        '    mFlatLeafSafe = false;\n'
                        '    ' + mutation + ' value;\n'
                        '    mFlatLeafSafe = preserveFlatLeaf;\n}')
    text = text.replace('    ' + mutation + ' nlohmann::json(array);\n}',
                        '    mFlatLeafSafe = false;\n    ' + mutation + ' nlohmann::json(array);\n}')
    if text.count('const bool preserveFlatLeaf =') != 5:
        raise ValueError('Config scalar setter count drift')
    text = once(text, '    mFlattenedJson.erase(FormatNestedKey(key));',
                '    mFlatLeafSafe = false;\n    mFlattenedJson.erase(FormatNestedKey(key));')
    text = once(text, '        mFlattenedJson[nestedToKey] = mFlattenedJson[nestedFromKey];',
                '        mFlatLeafSafe = false;\n        mFlattenedJson[nestedToKey] = mFlattenedJson[nestedFromKey];')
    reflatten = '    mFlattenedJson = gjson.flatten();'
    if text.count(reflatten) != 2:
        raise ValueError('Config block mutation count drift')
    text = text.replace(reflatten, '    mFlatLeafSafe = false;\n' + reflatten + '\n    mFlatLeafSafe = true;')
    text = once(text, '        mFlattenedJson = mNestedJson.flatten();',
                '        mFlattenedJson = mNestedJson.flatten();\n        mFlatLeafSafe = true;')
    text = once(text, '        mFlattenedJson = nlohmann::json::object();\n        return;',
                '        mFlattenedJson = nlohmann::json::object();\n        mFlatLeafSafe = true;\n        return;')
    text = once(text, '    } catch (...) { mFlattenedJson = nlohmann::json::object(); }',
                '    } catch (...) { mFlattenedJson = nlohmann::json::object(); mFlatLeafSafe = true; }')
    text = once(text, '    mNestedJson = mFlattenedJson.unflatten();\n    Shipyard::Storage::WriteAtomicFile(',
                '    mNestedJson = mFlattenedJson.unflatten();\n'
                '    mFlatLeafSafe = (mNestedJson.flatten() == mFlattenedJson);\n'
                '    Shipyard::Storage::WriteAtomicFile(')
    return text
