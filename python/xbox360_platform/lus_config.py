"""Write libultraship settings on a service thread instead of the saving caller.

Settings saves (every menu change, and scene or file hooks) otherwise
unflatten, pretty-print and flush the whole config on the calling thread,
which under game/render overlap is render's CheckSaveCvars. Config::Save now
only copies JSON; one service thread writes it. A newer request replaces one
that has not started, and Reload and the destructor wait for the writes.

Run after config_leaf_lookup.project_source when a consumer applies it.
"""

MATCH = """void Config::Save() {
    if (mPath == "None") return;
    mNestedJson = mFlattenedJson.unflatten();
%s    Shipyard::Storage::WriteAtomicFile(fs::path(mPath), mNestedJson.dump(4));
}"""
LEAF = "    mFlatLeafSafe = (mNestedJson.flatten() == mFlattenedJson);\n"

WRITER = """namespace {
// Config::Save hands this writer a copy of the settings; it never touches the
// Config. One request is pending at most: a newer save for the same file
// replaces one that has not started, and Wait returns once both are written.
class ShipyardConfigWriter {
  public:
    ShipyardConfigWriter() : mFlattened(false), mPending(false), mBusy(false), mStop(false) {
    }
    ~ShipyardConfigWriter() {
        {
            Shipyard::Async::unique_lock<Shipyard::Async::mutex> lock(mMutex);
            mStop = true;
        }
        mWake.notify_all();
        if (mWorker.joinable()) {
            mWorker.join();
        }
    }
    void Queue(const std::string& path, const nlohmann::json& document, bool flattened) {
        nlohmann::json copy(document);
        {
            Shipyard::Async::unique_lock<Shipyard::Async::mutex> lock(mMutex);
            // A pending save of another file is never dropped.
            while (mPending && mPath != path) {
                mIdle.wait(lock);
            }
            mPath = path;
            mDocument.swap(copy);
            mFlattened = flattened;
            mPending = true;
            if (!mWorker.joinable()) {
#ifdef _XBOX
                mWorker = Shipyard::Async::joining_thread(Run, this, Shipyard::Async::ServiceProcessor);
#else
                mWorker = Shipyard::Async::joining_thread(Run, this);
#endif
            }
        }
        mWake.notify_one();
    }
    void Wait() {
        Shipyard::Async::unique_lock<Shipyard::Async::mutex> lock(mMutex);
        while (mPending || mBusy) {
            mIdle.wait(lock);
        }
    }

  private:
    static void Run(void* context) {
        static_cast<ShipyardConfigWriter*>(context)->Loop();
    }
    void Loop() {
        Shipyard::Async::unique_lock<Shipyard::Async::mutex> lock(mMutex);
        for (;;) {
            while (!mPending && !mStop) {
                mWake.wait(lock);
            }
            if (!mPending) {
                return;
            }
            std::string path;
            nlohmann::json document;
            path.swap(mPath);
            document.swap(mDocument);
            const bool flattened = mFlattened;
            mPending = false;
            mBusy = true;
            lock.unlock();
            // A failed serialization leaves the previous file in place.
            try {
                const std::string text = flattened ? document.unflatten().dump(4) : document.dump(4);
                Shipyard::Storage::WriteAtomicFile(fs::path(path), text);
            } catch (const std::exception& failure) {
                SPDLOG_ERROR("Config save failed: {}", failure.what());
            } catch (...) {
                SPDLOG_ERROR("Config save failed");
            }
            lock.lock();
            mBusy = false;
            mIdle.notify_all();
        }
    }
    Shipyard::Async::mutex mMutex;
    Shipyard::Async::condition_variable mWake;
    Shipyard::Async::condition_variable mIdle;
    std::string mPath;
    nlohmann::json mDocument;
    bool mFlattened;
    bool mPending;
    bool mBusy;
    bool mStop;
    Shipyard::Async::joining_thread mWorker;
};
ShipyardConfigWriter sShipyardConfigWriter;
}

"""


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Config writer source drift: " + old[:80])
    return text.replace(old, new)


def background_save(text, nested=True, save=None, path="mPath"):
    """Queue Config::Save on ShipyardConfigWriter.

    save is the consumer's exact projected Save; by default the atomic-file
    form, with or without config_leaf_lookup's certificate line.
    nested keeps mNestedJson current on the caller. Pass nested=False only
    when every GetNestedJson reader runs at startup after Reload and before
    the first Save; the writer then also unflattens off the caller.
    """
    if save is None:
        save = MATCH % (LEAF if LEAF in text else "")
    if nested:
        # mFlatLeafSafe keeps its caller-side recertification when present.
        leaf = LEAF if LEAF in save else ""
        body = ("void Config::Save() {\n    if (mPath == \"None\") return;\n"
                "    mNestedJson = mFlattenedJson.unflatten();\n" + leaf +
                "    sShipyardConfigWriter.Queue(" + path + ", mNestedJson, false);\n}")
    else:
        # mNestedJson is then read only at startup and Reload replaces it.
        # mFlatLeafSafe is only an index validity flag: leaving it unchanged
        # is always correct.
        body = ("void Config::Save() {\n    if (mPath == \"None\") return;\n"
                "    sShipyardConfigWriter.Queue(" + path + ", mFlattenedJson, true);\n}")
    text = once(text, save, body)
    text = once(text, "void Config::Reload() {", "void Config::Reload() {\n    sShipyardConfigWriter.Wait();")
    # Defined before its first use (the destructor precedes Reload and Save);
    # a destroyed Config has published its last requested save.
    text = once(text, "Config::~Config() {", WRITER + "Config::~Config() {\n    sShipyardConfigWriter.Wait();")
    # After Config.h, so consumer compatibility preludes stay first.
    include = '#include "ship/config/Config.h"\n'
    return once(text, include, include + '#include "async/native.h"\n#include "storage/atomic_file.h"\n'
                "#include <spdlog/spdlog.h>\n")
