"""Adapt pinned spdlog 1.16 queue templates for the Xbox compiler."""

import hashlib
from pathlib import Path


QUEUES = ('details/circular_q.h', 'details/mpmc_blocking_q.h')


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Pinned logging queue form changed: ' + old[:80])
    return text.replace(old, new)


def project(*, source_root, locks, output_root, native_header):
    source_root = Path(source_root)
    output_root = Path(output_root)
    results = {}
    for name in QUEUES:
        source = source_root / name
        original = source.read_bytes()
        if hashlib.sha256(original).hexdigest() != locks[name]:
            raise ValueError('Pinned logging queue source changed: ' + name)
        text = original.decode().replace('\r\n', '\n')
        if name.endswith('circular_q.h'):
            text = replace_once(text, '#include "spdlog/common.h"',
                                '#include "' + Path(native_header).as_posix() + '"')
            text = text.replace(' SPDLOG_NOEXCEPT', '')
            for declaration in ('size_t max_items_',
                                'typename std::vector<T>::size_type head_',
                                'typename std::vector<T>::size_type tail_',
                                'size_t overrun_counter_'):
                text = replace_once(text, '    ' + declaration + ' = 0;',
                                    '    ' + declaration + ';')
            for old, new in (
                ('using value_type = T;', 'typedef T value_type;'),
                ('circular_q() = default;',
                 'circular_q() : max_items_(0), head_(0), tail_(0), overrun_counter_(0) {}'),
                ('v_(max_items_) {}',
                 'head_(0), tail_(0), overrun_counter_(0), v_(max_items_) {}'),
                ('circular_q(const circular_q &) = default;',
                 'circular_q(const circular_q &other) : max_items_(other.max_items_), '
                 'head_(other.head_), tail_(other.tail_), '
                 'overrun_counter_(other.overrun_counter_), v_(other.v_) {}'),
                ('circular_q &operator=(const circular_q &) = default;',
                 'circular_q &operator=(const circular_q &other) { max_items_=other.max_items_; '
                 'head_=other.head_; tail_=other.tail_; '
                 'overrun_counter_=other.overrun_counter_; v_=other.v_; return *this; }'),
            ):
                text = replace_once(text, old, new)
        else:
            first = text.index('#ifndef __MINGW32__')
            otherwise = text.index('\n#else', first)
            end = text.index('\n#endif', otherwise)
            text = text[:first] + text[first + len('#ifndef __MINGW32__'):otherwise] + text[end + len('\n#endif'):]
            first = text.index('    // dequeue with a timeout.')
            end = text.index('    // blocking dequeue without a timeout.', first)
            text = text[:first] + text[end:]
            for header in ('atomic', 'condition_variable', 'mutex'):
                text = replace_once(text, '#include <' + header + '>', '')
            for typ in ('mutex', 'unique_lock', 'lock_guard', 'condition_variable'):
                text = text.replace('std::' + typ, 'Shipyard::Logging::' + typ)
            for old, new in (
                ('using item_type = T;', 'typedef T item_type;'),
                (': q_(max_items) {}', ': q_(max_items), discard_counter_(0) {}'),
                ('++discard_counter_;', 'discard_counter_.fetch_add(1);'),
                ('std::atomic<size_t> discard_counter_{0};',
                 'Shipyard::Atomic::atomic<size_t> discard_counter_;'),
            ):
                text = replace_once(text, old, new)
            text = text.replace('.load(std::memory_order_relaxed)', '.load()')
            text = text.replace('.store(0, std::memory_order_relaxed)', '.store(0)')
        output = output_root / name
        output.parent.mkdir(parents=True, exist_ok=True)
        guard = 'SHIPYARD_LOG_QUEUE_' + name.replace('/', '_').replace('.', '_').upper()
        data = ('#ifndef ' + guard + '\n#define ' + guard + '\n#line 1 "' +
                source.as_posix() + '"\n' + text + '\n#endif\n').encode()
        output.write_bytes(data)
        results[name] = hashlib.sha256(data).hexdigest()
    return results
