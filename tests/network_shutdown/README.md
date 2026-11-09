# macOS discovery shutdown regression

The macOS network plugin `02.08.04.60` can leave SSDP receiver threads running
when `bambu_network_destroy_agent` frees its discovery object. Studio previously
called `start_discovery(false, false)` only on Windows. The fix calls it on the
main thread on all platforms before destroying callback consumers and the agent.

This test loads the user's installed plugin through its exported ABI, initializes
it with the same certificate file used by Studio, starts passive discovery, and
compares destruction with and without an explicit discovery stop. It uses fresh
temporary state and no account, printer credentials or printer control commands.
Guard Malloc makes reads of the freed discovery object fault deterministically.
It needs macOS, clang++, and an installed plugin; it is intentionally opt-in.

```sh
python3 tests/network_shutdown/run_plugin_lifecycle.py \
  '/path/to/plugins/libbambu_networking.dylib' \
  '/Applications/BambuStudio.app/Contents/Resources/cert'
```

The default is three subprocesses per mode: baseline, explicit stop, and
explicit stop immediately after discovery starts. Baseline must fault *after* agent
destruction returns; the stopped case must exit successfully after a six-second
post-destruction wait. Timeout, initialization failure, and pre-destruction faults
fail the test. A future fixed SDK may make the negative control pass normally;
that is not a reason to reintroduce the old application shutdown ordering.

In the affected ARM64 plugin (universal file SHA-256
`bfcc29b3dbdd1a1977e56cd13c0f50a395602286da6b3d667b5491952a8f2176`),
the original crash maps to offsets `0x8da04`, `0x8b8b0`, `0xa01ec`, and
`0xc45470`. These correspond to neighbor-list freeing, network-interface refresh,
the SSDP receiver loop, and its thread entry. The faulting instruction loads
`[x0 + 0x688]`, agreeing with the report's register/fault-address difference.
Guard Malloc catches the same receiver earlier at offset `0xa0140`, its read of
the freed object's stop flag. Function names inferred from adjacent embedded
strings are not vendor debug symbols; `ft_job_get_msg` is merely the nearest
export and is not the identity of the worker.

No proprietary plugin binary or user crash report is included.

## SDK-free shutdown ordering check

```sh
python3 tests/network_shutdown/test_shutdown_order.py
```

This source-contract check requires Python 3 and a C++ preprocessor (`c++`, or
`CXX`). It preprocesses the actual `GUI_App::OnExit` body for Linux, macOS and
Windows, with public and internal build flags. It checks closing notification,
joinable guards and ordering for both startup workers, a single null-guarded
discovery stop before callback consumers and the agent are deleted, and the
final configuration flush. In particular, the minors-decisions worker must be
joined before discovery shutdown and agent destruction.

This is not an application build or a threaded lifecycle test. It does not
validate proprietary SDK behavior, worker completion, or GUI shutdown on any
platform; the opt-in macOS regression above and application smoke tests are
still required for those behaviors.
