#include "fast3d/command_stack.h"
#include <vector>
#include <stdio.h>
#include <stdlib.h>

static void need(bool ok) { if(!ok) { fputs("command stack mismatch\n",stderr);exit(1); } }
struct F3DGfx { unsigned words[2]; };
static F3DGfx* portNormalizeDisplayListPointer(F3DGfx* p) { return p; }
static void diagRecordDLPush(F3DGfx*,F3DGfx*,F3DGfx*,const char*) {}
namespace Original {
// ORIGINAL_STACK
// ORIGINAL_METHODS
}
namespace Projected {
// PROJECTED_STACK
// PROJECTED_METHODS
}
static void same(const Original::GfxExecStack& a,const Projected::GfxExecStack& b) {
    need(a.cmd_stack.size()==b.cmd_stack.size());
    need(a.cmd_stack.empty()==b.cmd_stack.empty());
    need(a.gfx_path==b.gfx_path);
    std::stack<F3DGfx*> ac=a.cmd_stack,bc=b.cmd_stack;
    while(!ac.empty()) { need(ac.top()==bc.top());ac.pop();bc.pop(); }
    if(!b.cmd_stack.empty())need(b.cmd_stack.top()==a.cmd_stack.top());
}
int main() {
    F3DGfx commands[8192]={};
    Original::GfxExecStack a;Projected::GfxExecStack b;
    a.start(commands);b.start(commands);
    F3DGfx*& ar=a.currCmd();F3DGfx*& br=b.currCmd();
    // Deque slot references must survive pushes and map reallocations.
    for(unsigned i=0;i<4096;++i) { a.call(commands+i,commands+i+1);b.call(commands+i,commands+i+1); }
    ++ar;++br;same(a,b);
    while(!a.cmd_stack.empty()) { need(a.ret()==b.ret());same(a,b); }
    unsigned random=123;
    for(unsigned i=0;i<100000;++i) {
        random=random*1664525u+1013904223u;
        F3DGfx* cmd=commands+(random&4095);
        if(a.cmd_stack.empty()) {a.start(cmd);b.start(cmd);}
        switch((random>>16)%6) {
        case 0:a.call(cmd,cmd+1);b.call(cmd,cmd+1);break;
        case 1:a.currCmd()=cmd;b.currCmd()=cmd;a.branch(cmd);b.branch(cmd);break;
        case 2:need(a.ret()==b.ret());break;
        case 3:++a.currCmd();++b.currCmd();break;
        case 4:a.start(cmd);b.start(cmd);break;
        case 5:a.stop();b.stop();break;
        }
        same(a,b);
        Shipyard::Fast3D::CommandStack<F3DGfx*> copy=b.cmd_stack,assigned;
        assigned=copy;
        if(!copy.empty()) { copy.top()=commands+7000;need(assigned.top()==b.cmd_stack.top()); }
        copy.swap(assigned);
        if(!copy.empty())need(copy.top()==b.cmd_stack.top());
        assigned=assigned;
    }
    puts("100000 real stack-operation comparisons; deep nesting, parent references, branches, stop/restart, diagnostic copies, assignment/swap passed");
}
