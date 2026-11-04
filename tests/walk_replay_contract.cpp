// Recorded draws share one row stream that each replay patches; uploads after a sampling draw taint the walk.
#include <cassert>
#include <cstdio>
#include "fast3d/walk_replay.h"

int main() {
    using namespace Shipyard::Fast3D;
    WalkLog<3> log;
    int list;
    log.Begin(&list, 2);
    // Two rows of stride 8: clip position, then one texture coordinate pair at 6.
    const float a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 17, 18};
    const uint16_t corners[3] = {0, 1, 1};
    log.Selected(4);
    log.Uploaded(); // the fresh texture before any draw
    log.Patch(0, 2, 8, true, 6, 0);
    for (unsigned r = 0; r < 2; ++r) {
        float* v = log.Values(r, 12);
        for (int i = 0; i < 12; ++i) v[i] = 100.0f * (r + 1) + i;
    }
    log.Draw(a, 16, 1, corners, 2);
    log.Patch(0, 1, 8, false, 6, 0); // a rectangle row: texture coordinates only
    *log.Values(0, 2) = 50;
    *log.Values(1, 2) = 60;
    log.Draw(a, 8, 1, NULL, 0);
    log.Finish();
    assert(log.taint == WalkClean && log.Replays(&list, 0) && log.Replays(&list, 1));
    assert(!log.Replays(&list, 2) && !log.Replays(&corners, 0));
    const WalkOp& indexed = log.ops[0];
    const WalkOp& flat = log.ops[1];
    assert(log.Stream(indexed)[5] == 6 && log.Stream(flat)[0] == 1 && flat.at == 16);
    assert(log.Corners(indexed)[2] == 1 && log.Corners(flat) == NULL);
    log.Apply(1);
    const float* s = log.Stream(indexed);
    assert(s[0] == 200 && s[3] == 203 && s[4] == 5 && s[6] == 204 && s[7] == 205);
    assert(s[8] == 206 && s[14] == 210 && s[15] == 211 && log.Stream(flat)[6] == 60 && log.Stream(flat)[0] == 1);
    log.Apply(0);
    assert(s[0] == 100 && s[15] == 111 && log.Stream(flat)[6] == 50 && s[5] == 6);

    // A texture selected before a draw and rewritten after it changes that draw.
    log.Begin(&list, 2);
    log.Selected(4);
    log.Draw(a, 8, 1, NULL, 0);
    log.Selected(9);
    log.Uploaded();
    assert(log.taint == WalkClean);
    log.Selected(4);
    log.Uploaded();
    log.Finish();
    assert(log.taint == WalkTextureRewrite && !log.Replays(&list, 0));

    // Generations forget the previous walk; an upload without a selection taints.
    log.Begin(&list, 1);
    log.Draw(a, 8, 1, NULL, 0);
    log.Patch(0, 1, 8, true, 0, 0); // rows that never reach a draw
    log.Values(0, 4);
    log.Apply(0);
    assert(log.Stream(log.ops[0])[0] == 1 && log.rows.size() == 8);
    log.Selected(4);
    log.Uploaded();
    assert(log.taint == WalkClean);
    log.SelectedFramebuffer();
    log.Uploaded();
    assert(log.taint == WalkTextureRewrite);
    log.Taint(WalkBranchDiverged);
    assert(log.taint == WalkTextureRewrite);
    std::printf("walk replay contract passed\n");
    return 0;
}
