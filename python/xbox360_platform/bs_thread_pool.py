"""Source-locked BS thread-pool 4.1.0 adaptation for Shipyard::Async.

The caller owns the upstream header and the output directory. This module
only lowers the reviewed priority/pause subset; unsupported APIs are absent.
"""

import argparse
import hashlib
import json
from pathlib import Path


SOURCE_COMMIT = "097aa718f25d44315cadb80b407144ad455ee4f9"
SOURCE_SHA256 = "1da8bb1562433cb0759d774182d0d6fa3b87a04c26f64c6886f640445bf68efe"
SOURCE_LABEL = "BS_thread_pool.hpp"


def _normalized(raw):
    return raw.decode("utf-8-sig").replace("\r\n", "\n").encode("utf-8")


def _replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("Ambiguous or missing BS transformation anchor: " + old[:80])
    return source.replace(old, new)


def _project(source):
    chunks = []
    mappings = []

    def emit(body, line=1, operation="generated support"):
        chunks.append(f'#line {line} "{SOURCE_LABEL}"\n{body}\n')
        mappings.append({"operation": operation, "source_line": line})

    def extract(marker):
        if source.count(marker) != 1:
            raise ValueError("Ambiguous or missing BS method: " + marker)
        start = source.index(marker)
        opening = source.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            if end >= len(source):
                raise ValueError("Unbalanced BS method: " + marker)
            if source[end] == "{":
                depth += 1
            elif source[end] == "}":
                depth -= 1
            end += 1
        if source[end : end + 1] == ";":
            end += 1
        return source[start:end], source.count("\n", 0, start) + 1

    def lower(body):
        return (
            body.replace("[[nodiscard]] ", "")
            .replace("std::scoped_lock", "Shipyard::Async::lock_guard<Shipyard::Async::mutex>")
            .replace("std::unique_lock", "Shipyard::Async::unique_lock<Shipyard::Async::mutex>")
            .replace("std::future<", "Shipyard::Async::future<")
            .replace("std::promise<", "Shipyard::Async::promise<")
            .replace("std::thread::hardware_concurrency()", "Shipyard::Async::hardware_concurrency()")
        )

    def method(marker, transform=None):
        body, line = extract(marker)
        if transform:
            body = transform(body)
        emit(lower(body), line, marker.strip())

    emit(
        "\n".join(source.splitlines()[:12])
        + """
#include "thread-pool/native_support.h"
#include <queue>
#include <vector>
#if !defined(BS_THREAD_POOL_ENABLE_PRIORITY) || !defined(BS_THREAD_POOL_ENABLE_PAUSE)
#error This audited BS subset requires priority and pause.
#endif
#if defined(BS_THREAD_POOL_DISABLE_EXCEPTION_HANDLING) || defined(BS_THREAD_POOL_ENABLE_WAIT_DEADLOCK_CHECK) || defined(BS_THREAD_POOL_ENABLE_NATIVE_HANDLES)
#error Unsupported BS configuration in the Shipyard subset.
#endif
#define BS_THREAD_POOL_VERSION_MAJOR 4
#define BS_THREAD_POOL_VERSION_MINOR 1
#define BS_THREAD_POOL_VERSION_PATCH 0
#define BS_THREAD_POOL_PRIORITY_INPUT , const priority_t priority = 0
#define BS_THREAD_POOL_PRIORITY_OUTPUT , priority
#define BS_THREAD_POOL_PAUSED_OR_EMPTY (paused || tasks.empty())
namespace BS {
typedef std::size_t size_t;
typedef unsigned int concurrency_t;
typedef short priority_t;
namespace pr {
    const priority_t highest = 32767;
    const priority_t high = 16383;
    const priority_t normal = 0;
    const priority_t low = -16384;
    const priority_t lowest = -32768;
}
class thread_pool {
public:
""",
        operation="bounded include/API/configuration projection",
    )

    # VC2010 has neither delegating constructors nor in-class initializers.
    init = """\
paused(false), tasks_running(0), thread_count(determine_thread_count(NUM)), \
threads(new Shipyard::Async::joining_thread[thread_count]), waiting(false), workers_running(false)"""
    for signature, count, task, line in [
        ("thread_pool()", "0", "[] {}", 301),
        ("explicit thread_pool(const concurrency_t num_threads)", "num_threads", "[] {}", 308),
        ("explicit thread_pool(const std::function<void()>& init_task)", "0", "init_task", 315),
        (
            "thread_pool(const concurrency_t num_threads, const std::function<void()>& init_task)",
            "num_threads",
            "init_task",
            320,
        ),
    ]:
        emit(
            f"    {signature} : {init.replace('NUM', count)} {{ create_threads({task}); }}",
            line,
            "delegating constructor and member initialization lowering",
        )

    for marker in [
        "    ~thread_pool()",
        "    [[nodiscard]] size_t get_tasks_queued()",
        "    [[nodiscard]] size_t get_tasks_running()",
        "    [[nodiscard]] size_t get_tasks_total()",
        "    [[nodiscard]] concurrency_t get_thread_count()",
        "    [[nodiscard]] bool is_paused()",
        "    void pause()",
        "    void purge()",
    ]:
        method(marker)

    method(
        "    template <typename F>\n    void detach_task",
        lambda body: _replace_once(
            body,
            "tasks.emplace(std::forward<F>(task) BS_THREAD_POOL_PRIORITY_OUTPUT);",
            "tasks.push(pr_task(std::forward<F>(task) BS_THREAD_POOL_PRIORITY_OUTPUT));",
        ),
    )

    def lower_submit(body):
        body = _replace_once(
            body, "template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>", "template <typename F>"
        )
        body = _replace_once(
            body,
            "std::future<R> submit_task",
            "std::future<typename Shipyard::Async::invoke_result<F>::type> submit_task",
        )
        body = _replace_once(
            body,
            "        const std::shared_ptr<std::promise<R>>",
            "        typedef typename Shipyard::Async::invoke_result<F>::type R;\n"
            "        const typename std::decay<F>::type task_copy(std::forward<F>(task));\n"
            "        const std::shared_ptr<std::promise<R>>",
        )
        body = _replace_once(body, "[task = std::forward<F>(task), task_promise]", "[task_copy, task_promise]")
        begin = body.index("                    if constexpr")
        end = body.index("#ifndef BS_THREAD_POOL_DISABLE_EXCEPTION_HANDLING", begin)
        return (
            body[:begin]
            + "                    Shipyard::Async::complete_promise<R>::run(task_copy, task_promise);\n"
            + body[end:]
        )

    method(
        "    template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>\n    [[nodiscard]] std::future<R> submit_task",
        lower_submit,
    )
    method("    void unpause()")
    method("    void wait()\n")
    emit(
        "private:\n    thread_pool(const thread_pool&);\n    thread_pool& operator=(const thread_pool&);",
        327,
        "deleted special members lowered to inaccessible declarations",
    )

    def lower_create(body):
        body = _replace_once(
            body,
            "threads[i] = std::thread(&thread_pool::worker, this, i, init_task);",
            "threads[i] = Shipyard::Async::make_thread(std::bind(&thread_pool::worker, this, i, init_task));",
        )
        begin = body.index("        for (concurrency_t i")
        loop = body[begin : body.rfind("    }")]
        return body[:begin] + "        try {\n" + loop + "        } catch (...) { destroy_threads(); throw; }\n    }"

    method("    void create_threads(", lower_create)
    method("    void destroy_threads()")
    method("    [[nodiscard]] static concurrency_t determine_thread_count(")

    def lower_worker(body):
        body = _replace_once(body, "while (true)", "for (;;)")
        body = _replace_once(
            body,
            "        this_thread::get_index.index = idx;",
            "        (void)idx; // Optional BS thread-info APIs are outside this projection.",
        )
        body = _replace_once(body, "        this_thread::get_pool.pool = this;\n", "")
        body = _replace_once(body, "        this_thread::get_index.index = std::nullopt;\n", "")
        body = _replace_once(body, "        this_thread::get_pool.pool = std::nullopt;\n", "")
        body = _replace_once(
            body,
            """            tasks_lock.unlock();
            if (waiting && (tasks_running == 0) && BS_THREAD_POOL_PAUSED_OR_EMPTY)
                tasks_done_cv.notify_all();
            tasks_lock.lock();""",
            """            // Keep the completion predicate under tasks_mutex.
            if (waiting && (tasks_running == 0) && BS_THREAD_POOL_PAUSED_OR_EMPTY)
                tasks_done_cv.notify_all();""",
        )
        return _replace_once(body, "std::remove_const_t<pr_task&>(tasks.top())", "const_cast<pr_task&>(tasks.top())")

    method("    void worker(", lower_worker)
    method(
        "    class [[nodiscard]] pr_task",
        lambda body: _replace_once(
            _replace_once(body, "std::function<void()> task = {};", "std::function<void()> task;"),
            "priority_t priority = 0;",
            "priority_t priority;",
        ),
    )
    emit(
        """
    bool paused;
    Shipyard::Async::condition_variable task_available_cv;
    Shipyard::Async::condition_variable tasks_done_cv;
    std::priority_queue<pr_task> tasks;
    size_t tasks_running;
    mutable Shipyard::Async::mutex tasks_mutex;
    concurrency_t thread_count;
    std::unique_ptr<Shipyard::Async::joining_thread[]> threads;
    bool waiting;
    bool workers_running;
};
} // namespace BS
#endif
""",
        1098,
        "member types and constructor initialization lowering",
    )
    return "\n".join(chunks), mappings


def generate(source_path, output_dir):
    """Generate BS_thread_pool.hpp and provenance.json from a pinned upstream header."""
    source_path = Path(source_path)
    output_dir = Path(output_dir)
    raw = _normalized(source_path.read_bytes())
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError("Refusing changed BS source: " + digest)
    generated, mappings = _project(raw.decode("utf-8"))
    manifest = {
        "source": SOURCE_LABEL,
        "source_commit": SOURCE_COMMIT,
        "source_sha256": digest,
        "generator_sha256": hashlib.sha256(_normalized(Path(__file__).read_bytes())).hexdigest(),
        "generated_sha256": hashlib.sha256(generated.encode("utf-8")).hexdigest(),
        "projection": mappings,
        "semantic_corrections": [
            "worker completion predicate remains under tasks_mutex",
            "failed worker creation signals stop and joins created workers before rethrow",
        ],
        "unsupported": [
            "reset",
            "pool timed waits",
            "multi_future",
            "block/loop/sequence helpers",
            "this_thread optional TLS queries",
            "native handle/id queries",
            "wait deadlock-check option",
        ],
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / SOURCE_LABEL).write_text(generated, encoding="utf-8", newline="\n")
    (output_dir / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    generate(args.source, args.output)


if __name__ == "__main__":
    main()
