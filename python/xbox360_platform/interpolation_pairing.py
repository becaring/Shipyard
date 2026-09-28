"""Prepare immutable recording pairs once; retain consumer replay operations verbatim."""
import re
from .resource_lookup import _once


def project(source, *, value_expression):
    start=source.index('    void interpolate_branch(')
    end=source.index('\n};',start)
    branch=source[start:end]
    switch_start=branch.index('                    switch (')
    brace=branch.index('{',switch_start)
    depth=1; cursor=brace+1
    while depth:
        depth += (branch[cursor]=='{') - (branch[cursor]=='}')
        cursor+=1
    switch=branch[switch_start:cursor]
    switch=switch.replace(value_expression.format(value='item.first'),value_expression.format(value='entry.op'),1)
    switch=re.sub(r'new_replacement\(new_op\.(?:matrix_mtxf_to_mtx|matrix_to_mtx)\.dest\)', 'destination',switch)
    prepare=branch[:switch_start]+"""                    PreparedInterpolationEntry entry;
                    entry.op = item.first;
                    entry.old_op = &old_op;
                    entry.new_op = &new_op;
                    entry.self_paired = old_path == new_path;
                    entry.destination = static_cast<size_t>(-1);
                    Mtx* address = NULL;
                    bool writes_matrix = false;
                    if (item.first == Op::MatrixMtxFToMtx) {
                        address = new_op.matrix_mtxf_to_mtx.dest; writes_matrix = true;
                    } else if (item.first == Op::MatrixToMtx) {
                        address = new_op.matrix_to_mtx.dest; writes_matrix = true;
                    }
                    if (writes_matrix) {
                        std::map<Mtx*, size_t>::iterator found = prepared_destination_index.find(address);
                        if (found == prepared_destination_index.end()) {
                            entry.destination = prepared_destinations.size();
                            prepared_destination_index[address] = entry.destination;
                            prepared_destinations.push_back(address);
                        } else entry.destination = found->second;
                    }
                    prepared_interpolation.push_back(entry);"""+branch[cursor:]
    prepare=prepare.replace('interpolate_branch(', 'prepare_branch(')
    replay="""
    void interpolate_branch(Path* old_path, Path* new_path) {
        if (!prepared_interpolation_valid) {
            prepare_branch(old_path, new_path);
            prepared_interpolation_valid = true;
        }
        // Each presentation owns its output map: Modern retains a whole batch.
        // Pair matching, duplicate destination grouping and replay order are shared.
        std::vector<MtxF*> slots;
        slots.reserve(prepared_destinations.size());
        for (size_t i = 0; i < prepared_destinations.size(); ++i)
            slots.push_back(&mtx_replacements[prepared_destinations[i]]);
        for (size_t i = 0; i < prepared_interpolation.size(); ++i) {
            const PreparedInterpolationEntry& entry = prepared_interpolation[i];
            Data& old_op = *entry.old_op;
            Data& new_op = *entry.new_op;
            const bool self_paired = entry.self_paired;
            MtxF* destination = entry.destination == static_cast<size_t>(-1) ? NULL : slots[entry.destination];
"""+switch+"""
        }
    }
"""
    source=source[:start]+prepare+replay+source[end:]
    source=_once(source,'struct InterpolateCtx {',"""struct PreparedInterpolationEntry {
    Op op;
    Data* old_op;
    Data* new_op;
    bool self_paired;
    size_t destination;
};
static std::vector<PreparedInterpolationEntry> prepared_interpolation;
static std::vector<Mtx*> prepared_destinations;
static std::map<Mtx*, size_t> prepared_destination_index;
static bool prepared_interpolation_valid = false;

struct InterpolateCtx {""")
    source=_once(source,'void FrameInterpolation_StartRecord(void) {',"""void FrameInterpolation_StartRecord(void) {
    prepared_interpolation_valid = false;
    prepared_interpolation.clear();
    prepared_destinations.clear();
    prepared_destination_index.clear();""")
    return source
