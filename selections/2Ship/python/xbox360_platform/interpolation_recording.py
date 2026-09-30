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


def pooled_containers(text):
    text = _once(text, '''    map<label, vector<Path>> children;
    map<Op, vector<Data>> ops;
    vector<pair<Op, size_t>> items;''', '''    typedef vector<Path, Shipyard::Interpolation::RecordingAllocator<Path>> Paths;
    typedef vector<Data, Shipyard::Interpolation::RecordingAllocator<Data>> Operations;
    typedef pair<Op, size_t> Item;
    map<label, Paths, less<label>, Shipyard::Interpolation::RecordingAllocator<pair<const label, Paths>>> children;
    map<Op, Operations, less<Op>, Shipyard::Interpolation::RecordingAllocator<pair<const Op, Operations>>> ops;
    vector<Item, Shipyard::Interpolation::RecordingAllocator<Item>> items;''')
    return '#include "interpolation/recording_memory.h"\n' + text


def retain_root_operations(text, *, capacity_bytes):
    """Retain bounded root-vector capacity across the two recording buffers.

    Requires recording_op_index: swap already rebound stable map-node pointers.
    Children and item order are cleared normally; retained vectors contain no
    operations. The live-capacity budget is separate from the freed-block pool.
    """
    if not isinstance(capacity_bytes, int) or not 0 < capacity_bytes <= 16 * 1024 * 1024:
        raise ValueError('Invalid root recording capacity budget')
    return _once(text, '''    current_recording.root_path.ops.clear();
    current_recording.root_path.items.clear();
    current_recording.root_path.ClearIndex();''', '''    size_t remainingRootCapacity = ''' + str(capacity_bytes) + ''';
    for (auto it = current_recording.root_path.ops.begin(); it != current_recording.root_path.ops.end(); ++it) {
        if (it->second.capacity() <= remainingRootCapacity / sizeof(Data)) {
            remainingRootCapacity -= it->second.capacity() * sizeof(Data);
            it->second.clear();
        } else {
            Path::Operations empty;
            it->second.swap(empty);
        }
    }
    current_recording.root_path.items.clear();''')


def recording_op_index(text, *, value_expression):
    # Map nodes remain stable. Cache their operation-vector addresses within
    # each Path and rebind after every ownership transfer or copy.
    text=_once(text,'    Path() {}\n    explicit Path(RecordingInPlace) {}', '''    Operations* opIndex[Op::SkinMatrixMtxFToMtx + 1];
    void ClearIndex() { memset(opIndex, 0, sizeof(opIndex)); }
    void RebindIndex() {
        ClearIndex();
        for (auto it = ops.begin(); it != ops.end(); ++it)
            opIndex[static_cast<unsigned>(it->first.GetValue())] = &it->second;
    }
    Operations& AppendOperations(Op op) {
        Operations*& found = opIndex[static_cast<unsigned>(op.GetValue())];
        if (!found) found = &ops[op];
        return *found;
    }
    Path() { ClearIndex(); }
    explicit Path(RecordingInPlace) { ClearIndex(); }''')
    text=_once(text,'items(other.items) {}','items(other.items) { RebindIndex(); }')
    text=_once(text,'void swap(Path& other) { children.swap(other.children); ops.swap(other.ops); items.swap(other.items); }',
        'void swap(Path& other) { children.swap(other.children); ops.swap(other.ops); items.swap(other.items); RebindIndex(); other.RebindIndex(); }')
    text=_once(text,'            other.children.clear(); other.ops.clear(); other.items.clear();',
        '            other.children.clear(); other.ops.clear(); other.items.clear(); other.ClearIndex();')
    text=_once(text,'    current_recording.root_path.items.clear();',
        '    current_recording.root_path.items.clear();\n    current_recording.root_path.ClearIndex();')
    text=_once(text,'auto& m = current_path.back()->ops[op];',
        'auto& m = current_path.back()->AppendOperations(op);')
    return text.replace("it->first.GetValue()", value_expression.format(value="it->first")).replace("op.GetValue()", value_expression.format(value="op"))

def replay_op_index(text, *, value_expression):
    # Every item is appended after its operation vector exists. RebindIndex
    # restores these addresses whenever ownership of a Path changes.
    start=text.index('    void interpolate_branch(')
    end=text.index('\n};',start)
    body=text[start:end]
    body=_once(body,'            Data& new_op = new_path->ops[item.first][item.second];',
              '            const unsigned op_index = static_cast<unsigned>(item.first.GetValue());\n'
              '            Data& new_op = (*new_path->opIndex[op_index])[item.second];')
    body=_once(body,'''            if (auto it = old_path->ops.find(item.first); it != old_path->ops.end()) {
                if (item.second < it->second.size()) {
                    Data& old_op = it->second[item.second];''',
              '''            Path::Operations* old_operations = old_path->opIndex[op_index];
            if (old_operations && item.second < old_operations->size()) {
                    Data& old_op = (*old_operations)[item.second];''')
    body=_once(body,'''                    }
                }
            }
        }
    }''','''                    }
            }
        }
    }''')
    body=body.replace("item.first.GetValue()", value_expression.format(value="item.first"))
    return text[:start]+body+text[end:]


def reuse_child_lookup(text):
    expression='&new_path->children.find(new_op.open_child.key)->second[new_op.open_child.idx]'
    start=text.index('    void interpolate_branch(')
    end=text.index('            if (auto it = old_path->ops.find',start)
    body=text[start:end]
    if body.count(expression)!=3:
        raise ValueError('Interpolation child lookup inventory drift')
    body=body.replace(expression,'new_child')
    anchor='            if (item.first == Op::OpenChild) {'
    body=_once(body,anchor,anchor+'\n                Path* new_child = '+expression+';')
    return text[:start]+body+text[end:]
