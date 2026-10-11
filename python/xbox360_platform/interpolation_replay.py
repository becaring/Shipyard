"""Trim and recycle prepared interpolation replay from interpolation_pairing.

Pruning drops matrix-stack work inside a balanced push/pop region that records no
actor matrix; output writes, replay order and the matrix after the pop are kept.
Recycling reuses a retired output map only when it holds every prepared
destination; replay overwrites each value before publication. The pool owns only
maps the game returns at frame retirement through FrameInterpolation_Recycle.
"""
from .resource_lookup import _once

PRUNE = r"""static bool DiscardedInterpolationStackOp(Op op) {
    switch (%s) {
        case Op::MatrixPush:
        case Op::MatrixPop:
        case Op::MatrixPut:
        case Op::MatrixMult:
        case Op::MatrixTranslate:
        case Op::MatrixScale:
        case Op::MatrixRotate1Coord:
        case Op::MatrixRotateZYX:
        case Op::MatrixTranslateRotateZYX:
        case Op::MatrixSetTranslateRotateYXZ:
        case Op::MatrixReplaceRotation:
        case Op::MatrixRotateAxis:
            return true;
        default:
            return false;
    }
}

static void PruneDiscardedInterpolationStack() {
    if (is_recording || prepared_interpolation.empty()) return;
    const size_t count = prepared_interpolation.size();
    std::vector<std::pair<size_t, size_t>> pushes;
    std::vector<int> discarded(count + 1, 0);
    size_t captures = 0;
    bool changed = false;
    for (size_t i = 0; i < count; ++i) {
        const PreparedInterpolationEntry& entry = prepared_interpolation[i];
        if (entry.op == Op::MatrixPush) {
            pushes.push_back(std::make_pair(i, captures));
        } else if (entry.op == Op::MatrixPop) {
            if (!pushes.empty()) {
                const std::pair<size_t, size_t> push = pushes.back();
                pushes.pop_back();
                if (push.second == captures) {
                    ++discarded[push.first];
                    --discarded[i + 1];
                    changed = true;
                }
            }
        } else if (entry.op == Op::MatrixSetTranslateRotateYXZ &&
                   entry.old_op->matrix_set_translate_rotate_yxz.has_mtx &&
                   entry.new_op->matrix_set_translate_rotate_yxz.has_mtx) {
            ++captures;
        }
    }
    if (!changed) return;
    size_t kept = 0;
    int depth = 0;
    for (size_t i = 0; i < count; ++i) {
        depth += discarded[i];
        if (depth && DiscardedInterpolationStackOp(prepared_interpolation[i].op)) continue;
        if (kept != i) prepared_interpolation[kept] = prepared_interpolation[i];
        ++kept;
    }
    prepared_interpolation.resize(kept);
}

"""

POOL = r"""class RecycledInterpolationMaps {
    typedef std::unordered_map<Mtx*, MtxF> Map;
    enum { MapCount = 8, MapLimit = 1024, EntryLimit = 4096 };
    Map maps[MapCount];
    size_t entries;
    unsigned next;
#ifdef _XBOX
    CRITICAL_SECTION gate;
    void Lock() { EnterCriticalSection(&gate); }
    void Unlock() { LeaveCriticalSection(&gate); }
#else
    std::mutex gate;
    void Lock() { gate.lock(); }
    void Unlock() { gate.unlock(); }
#endif
    struct Guard {
        RecycledInterpolationMaps& pool;
        explicit Guard(RecycledInterpolationMaps& value) : pool(value) { pool.Lock(); }
        ~Guard() { pool.Unlock(); }
    };
    void Drop(unsigned index) {
        entries -= maps[index].size();
        maps[index].clear();
    }
public:
    RecycledInterpolationMaps() : entries(0), next(0) {
#ifdef _XBOX
        InitializeCriticalSection(&gate);
#endif
    }
    ~RecycledInterpolationMaps() {
#ifdef _XBOX
        DeleteCriticalSection(&gate);
#endif
    }
    bool Acquire(Map& output, const std::vector<Mtx*>& destinations,
                 std::vector<MtxF*>& slots) {
        if (destinations.empty() || destinations.size() > MapLimit) return false;
        Guard guard(*this);
        for (unsigned index = 0; index < MapCount; ++index) {
            Map& candidate = maps[index];
            if (candidate.size() != destinations.size()) continue;
            slots.clear();
            for (size_t i = 0; i < destinations.size(); ++i) {
                Map::iterator found = candidate.find(destinations[i]);
                if (found == candidate.end()) break;
                slots.push_back(&found->second);
            }
            if (slots.size() == destinations.size()) {
                entries -= candidate.size();
                output.swap(candidate);
                return true;
            }
        }
        slots.clear();
        return false;
    }
    void Recycle(Map& input) {
        if (input.empty() || input.size() > MapLimit) return;
        Guard guard(*this);
        unsigned index = 0;
        while (index < MapCount && !maps[index].empty()) ++index;
        if (index == MapCount) {
            index = next;
            next = (next + 1) % MapCount;
            Drop(index);
        }
        while (entries + input.size() > EntryLimit) {
            Drop(next);
            next = (next + 1) % MapCount;
        }
        entries += input.size();
        maps[index].swap(input);
    }
};
static RecycledInterpolationMaps recycled_interpolation_maps;
"""


def prune_discarded_stack(source, *, value_expression):
    source = _once(source, 'struct InterpolateCtx {',
                   PRUNE % value_expression.format(value='op') + 'struct InterpolateCtx {')
    return _once(source,
                 '            prepare_branch(old_path, new_path);\n            prepared_interpolation_valid = true;',
                 '            prepare_branch(old_path, new_path);\n'
                 '            PruneDiscardedInterpolationStack();\n'
                 '            prepared_interpolation_valid = true;')


def recycle_maps(source):
    source = _once(source, 'struct InterpolateCtx {', POOL + '\nstruct InterpolateCtx {')
    source = _once(source, """        for (size_t i = 0; i < prepared_destinations.size(); ++i)
            slots.push_back(&mtx_replacements[prepared_destinations[i]]);""",
                   """        if (!recycled_interpolation_maps.Acquire(mtx_replacements, prepared_destinations, slots)) {
            for (size_t i = 0; i < prepared_destinations.size(); ++i)
                slots.push_back(&mtx_replacements[prepared_destinations[i]]);
        }""")
    source = _once(source, 'unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step) {',
                   """void FrameInterpolation_Recycle(std::unordered_map<Mtx*, MtxF>& replacements) {
    recycled_interpolation_maps.Recycle(replacements);
}

unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step) {""")
    return '#ifdef _XBOX\n#include <xtl.h>\n#else\n#include <mutex>\n#endif\n' + source
