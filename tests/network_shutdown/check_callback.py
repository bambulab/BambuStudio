#!/usr/bin/env python3
"""Exercise the production callback adapter without loading the proprietary SDK.

Usage: python3 tests/network_shutdown/check_callback.py [GUI_App.cpp]
Requires clang++ and AddressSanitizer. No account or network is used.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'src/slic3r/GUI/GUI_App.cpp'
text = source.read_text()
start = text.index('        m_agent->set_queue_on_main_fn(')
end = text.index('\n        });', start) + len('\n        });')
adapter = text[start:end]
harness = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
struct Agent {
    std::function<void(std::function<void()>)> submit;
    void set_queue_on_main_fn(decltype(submit) fn) { submit = std::move(fn); }
};
struct App {
    Agent agent;
    Agent *m_agent = &agent;
    std::atomic<bool> closing{false};
    std::vector<std::function<void()>> events;
    bool is_closing() const { return closing.load(std::memory_order_acquire); }
    void CallAfter(std::function<void()> fn) { events.push_back(std::move(fn)); }
    void init() { ADAPTER }
    void drain() {
        auto work = std::move(events);
        events.clear();
        for (auto &fn : work) fn();
    }
};
int main() {
    App app;
    app.init();
    int calls = 0;
    app.agent.submit([&] { ++calls; });
    assert(calls == 0); // ordinary callbacks remain asynchronous
    app.drain();
    assert(calls == 1);
    auto owner = std::make_unique<int>(42);
    int *raw = owner.get();
    app.agent.submit([raw] { assert(*raw == 42); });
    app.closing.store(true, std::memory_order_release);
    owner.reset();
    app.drain(); // baseline runs a callback against its destroyed owner
    app.agent.submit([&] { ++calls; });
    assert(app.events.empty()); // no fresh events after shutdown
    app.drain();
    assert(calls == 1);
}
'''.replace('ADAPTER', adapter)
with tempfile.TemporaryDirectory(prefix='network-callback-test-') as temp:
    root = pathlib.Path(temp)
    cpp = root / 'test.cpp'
    cpp.write_text(harness)
    exe = root / 'test'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: normal dispatch, queued callback after owner destruction, submission after shutdown')
