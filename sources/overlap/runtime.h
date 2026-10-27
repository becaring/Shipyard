// Shared game/render overlap session: one game worker builds packet N+1 while the
// title thread replays packet N. GUI is built once per packet, passively, at the
// first presentation while the game waits at its handoff; every replay draws that
// capture with its own game image. Interactive GUI and hotkey intents stop at the
// handoff and replay after the worker joins.
//
// Derived (CRTP) supplies:
//   static void Log(const char* what, const char* detail);
//   static bool Requested();          // render, game idle
//   static bool InteractiveGui();     // render, game idle
//   static void CommitGuiIntent();    // render, worker joined
//   static uintptr_t GameTexture();   // image the cached GUI composes
//   void BuildFrame();                // game worker; publishes through worker
//   void RenderReplays(const Ticket&, const Packet&);  // render
//   bool DrawPassiveGui();            // render, game idle, under capture
//   void CaptureInput(); void CaptureVariables(); void ReleaseVariables();
// and may hide Start(), CaptureGraphics() and BeforeFence().
// Capture supplies Cache, Begin(Cache*, bool suppress), End() and Draw(Cache&, uintptr_t).
// Packet supplies Owners and a `resources` member of that type.
// Include in one translation unit per program: the thread-local state is per include.
#ifndef SHIPYARD_OVERLAP_RUNTIME_H
#define SHIPYARD_OVERLAP_RUNTIME_H
#include "overlap/frame_worker.h"
#include "overlap/input_blocked.h"
#include "overlap/resource_owners.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <exception>
#include <memory>
#include <stdexcept>

namespace Shipyard {
namespace Overlap {
namespace RuntimeThread {
// Keep the worker identity through retirement destructors between callbacks.
static __declspec(thread) bool onGameWorker = false;
static __declspec(thread) const void* renderingPacket = NULL;
static __declspec(thread) void* renderOwners = NULL;
}

template <class Derived, class Packet, class Capture> class Runtime {
public:
    typedef FrameWorker<Packet> GameWorker;
    typedef typename GameWorker::Ticket Ticket;
    typedef typename Packet::Owners Owners;
    typedef typename Capture::Cache Cache;

    GameWorker worker;
    InputBlocked input;
    Owners renderResources;
    static Derived* session;

    Runtime() : worker(Entry, NULL) {}

    static bool OnGameWorker() { return RuntimeThread::onGameWorker; }
    static const Packet* RenderingPacket() { return static_cast<const Packet*>(RuntimeThread::renderingPacket); }
    static void Abort(const char* operation) {
        Derived::Log("Game/render overlap stopped", operation);
        abort();
    }
    // Call only inside a catch block.
    static void LogException(const char* where) {
        try { throw; }
        catch (const std::exception& e) { Derived::Log(where, e.what()); }
        catch (...) { Derived::Log(where, "non-standard exception"); }
    }

    static void Fence() {
        if (!RuntimeThread::onGameWorker) return;
        Derived::BeforeFence();
        if (!session->worker.fencePreviousReplay()) Abort("Game replay fence aborted");
    }
    static void RunOnRender(void (*callback)(void*), void* context) {
        if (!callback) return;
        if (!RuntimeThread::onGameWorker) { callback(context); return; }
        Fence();
        if (!session->worker.runOnRender([callback, context]() { callback(context); }))
            Abort("Native render job aborted");
    }
    static int InputBlocked(unsigned kind, int* value) {
        return RuntimeThread::onGameWorker ? session->input.Read(kind, value) : 0;
    }
    template <class Resource> static void Retain(const std::shared_ptr<Resource>& resource) {
        if (!resource) return;
        if (RuntimeThread::onGameWorker) session->worker.buildingPacket().resources.push_back(resource);
        else if (RuntimeThread::renderOwners) static_cast<Owners*>(RuntimeThread::renderOwners)->push_back(resource);
    }
    // Gui::StartDraw passes 1 and Gui::EndDraw 0; nonzero skips the live GUI.
    static int DrawGui(int begin) {
        if (!RuntimeThread::renderingPacket) return 0;
        if (begin && handoff) BeginGui(*handoff);
        if (!begin && replayGui) Capture::Draw(*replayGui, Derived::GameTexture());
        return 1;
    }
    // Returns 0 when the caller must run this frame sequentially.
    static int RunFrame() {
        try { return RunFrameLoggingFaults(); }
        catch (...) {
            LogException("Frame handoff threw");
            Abort("Frame handoff failed");
        }
        return 1;
    }
    static void Shutdown() {
        try {
            if (capturingWarmup) {
                Capture::End();
                capturingWarmup = false;
            }
            if (session) {
                Ticket ticket;
                const Packet* packet;
                if (session->worker.acquireReady(ticket, packet)) RenderPacket(ticket, *packet, true);
                EndSession();
            }
            passiveGui.reset();
        } catch (...) {
            LogException("Frame shutdown threw");
            Abort("Frame shutdown failed");
        }
    }

    void Start() {}
    void CaptureGraphics() {}
    static void BeforeFence() {}

private:
    struct Handoff {
        const Ticket& ticket;
        bool forceStop, started, submitted;
        Handoff(const Ticket& frame, bool stop) : ticket(frame), forceStop(stop), started(false), submitted(false) {}
    private:
        Handoff& operator=(const Handoff&);
    };
    static std::shared_ptr<Cache> passiveGui;
    static Cache* replayGui;
    static Handoff* handoff;
    static bool capturingWarmup, deferredGuiIntent;

    static void Entry(void*) {
        RuntimeThread::onGameWorker = true;
        try {
            session->BuildFrame();
        } catch (...) {
            LogException("Game frame threw");
            throw;
        }
    }
    // The CVar snapshot and the permit follow the GUI so both see its writes.
    static void Submit(Handoff& state) {
        if (state.submitted) return;
        state.submitted = true;
        session->CaptureVariables();
        if (!state.forceStop) session->worker.permitNext(state.ticket);
    }
    static void BeginGui(Handoff& state) {
        if (state.started) return;
        state.started = true;
        std::shared_ptr<Cache> next(new Cache());
        Capture::Begin(next.get(), true);
        const bool passive = session->DrawPassiveGui();
        const bool captured = Capture::End();
        if (captured) passiveGui = next;
        state.forceStop = state.forceStop || !passive || !captured;
        replayGui = passiveGui.get();
        session->CaptureInput();
        session->CaptureGraphics();
        Submit(state);
    }
    static bool RenderPacket(const Ticket& ticket, const Packet& packet, bool forceStop) {
        Derived& owner = *session;
        Handoff state(ticket, forceStop);
        {
            ResourceReaderScope<Owners> reader(owner.renderResources);
            RuntimeThread::renderingPacket = &packet;
            RuntimeThread::renderOwners = &owner.renderResources;
            replayGui = passiveGui.get();
            handoff = &state;
            owner.RenderReplays(ticket, packet);
            // Dropped presentations never reached the GUI hook.
            Submit(state);
            handoff = NULL;
            replayGui = NULL;
            RuntimeThread::renderingPacket = NULL;
            RuntimeThread::renderOwners = NULL;
        }
        owner.ReleaseVariables();
        if (!owner.worker.retire(ticket)) throw std::runtime_error("Replay retirement failed");
        if (state.forceStop) {
            deferredGuiIntent = state.started;
            owner.worker.stopAtHandoff(ticket);
        }
        return !state.forceStop;
    }
    static void EndSession() {
        session->worker.joinPumping();
        const bool intent = deferredGuiIntent;
        deferredGuiIntent = false;
        delete session;
        session = NULL;
        passiveGui.reset();
        if (intent) Derived::CommitGuiIntent();
    }
    static int RunFrameInternal() {
        if (capturingWarmup) {
            if (!Capture::End()) passiveGui.reset();
            capturingWarmup = false;
        }
        if (!session) {
            if (!Derived::Requested() || Derived::InteractiveGui()) {
                passiveGui.reset();
                return 0;
            }
            // One sequential frame captures the GUI the first packet replays.
            if (!passiveGui) {
                passiveGui.reset(new Cache());
                Capture::Begin(passiveGui.get(), false);
                capturingWarmup = true;
                return 0;
            }
            session = new Derived();
            deferredGuiIntent = false;
            session->Start();
            session->CaptureInput();
            session->CaptureGraphics();
            // Render stays on processor 0; consumers keep mixers and services off 4.
            session->worker.start(4);
        }
        Ticket ticket;
        const Packet* packet;
        if (!session->worker.acquireReady(ticket, packet) || !RenderPacket(ticket, *packet, !Derived::Requested()))
            EndSession();
        return 1;
    }
#ifdef _XBOX
    // catch(...) also takes hardware faults; log them while the frame is on the stack.
    static int LogFault(EXCEPTION_POINTERS* info) {
        char detail[64];
        _snprintf(detail, sizeof(detail), "code=%08lx address=%p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
        detail[sizeof(detail) - 1] = 0;
        Derived::Log("Frame handoff fault", detail);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static int RunFrameLoggingFaults() {
        __try { return RunFrameInternal(); }
        __except (LogFault(GetExceptionInformation())) { return 1; }
    }
#else
    static int RunFrameLoggingFaults() { return RunFrameInternal(); }
#endif
    Runtime(const Runtime&);
    Runtime& operator=(const Runtime&);
};

template <class D, class P, class C> D* Runtime<D, P, C>::session = NULL;
template <class D, class P, class C> std::shared_ptr<typename C::Cache> Runtime<D, P, C>::passiveGui;
template <class D, class P, class C> typename C::Cache* Runtime<D, P, C>::replayGui = NULL;
template <class D, class P, class C> typename Runtime<D, P, C>::Handoff* Runtime<D, P, C>::handoff = NULL;
template <class D, class P, class C> bool Runtime<D, P, C>::capturingWarmup = false;
template <class D, class P, class C> bool Runtime<D, P, C>::deferredGuiIntent = false;
}
}

#endif
