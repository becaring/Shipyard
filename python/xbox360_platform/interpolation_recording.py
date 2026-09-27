"""Transfer interpolation recording containers on compilers without implicit moves."""


def _once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('interpolation ownership contract changed: ' + old[:80])
    return source.replace(old, new, 1)


def ownership(source, *, move_expression):
    """Select only ownership changes shared by the Zelda recording contracts.

    The consumer supplies its pinned spelling of the frame rotation. Payloads,
    operation ordering, child labels, and interpolation calculations are retained.
    """
    source = _once(source, '''struct Path {
    map<label, vector<Path>> children;
    map<Op, vector<Data>> ops;
    vector<pair<Op, size_t>> items;
};''', '''struct RecordingInPlace {};
struct Path {
    map<label, vector<Path>> children;
    map<Op, vector<Data>> ops;
    vector<pair<Op, size_t>> items;
    Path() {}
    explicit Path(RecordingInPlace) {}
    Path(const Path& other) : children(other.children), ops(other.ops), items(other.items) {}
    void swap(Path& other) { children.swap(other.children); ops.swap(other.ops); items.swap(other.items); }
    Path& operator=(const Path& other) {
        if (this != &other) { Path copy(other); swap(copy); }
        return *this;
    }
    Path(Path&& other) { swap(other); }
    Path& operator=(Path&& other) {
        if (this != &other) {
            swap(other);
            other.children.clear(); other.ops.clear(); other.items.clear();
        }
        return *this;
    }
};''')
    source = _once(source,
        '    previous_recording = '+move_expression+'(current_recording);\n    current_recording = {};',
        '''    previous_recording.root_path.swap(current_recording.root_path);
    current_recording.root_path.children.clear();
    current_recording.root_path.ops.clear();
    current_recording.root_path.items.clear();''')
    source = _once(source, 'current_path.push_back(&m.emplace_back());',
        'm.emplace_back(RecordingInPlace());\n    current_path.push_back(&m.back());')
    return _once(source, 'return ctx.mtx_replacements;', 'return std::move(ctx.mtx_replacements);')
