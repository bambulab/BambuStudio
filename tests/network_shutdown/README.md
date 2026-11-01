# Network callback shutdown regression

Run `python3 tests/network_shutdown/check_callback.py` from any directory with
clang++ available. The script compiles the actual `set_queue_on_main_fn` adapter
extracted from GUI_App.cpp with a minimal FIFO dispatcher and runs it under
AddressSanitizer and UndefinedBehaviorSanitizer. It fails loudly if the source
adapter cannot be found. The proprietary SDK, credentials and wxWidgets are not
required.

The test verifies asynchronous dispatch while running, a queued callback whose
owner is destroyed after shutdown begins, and rejection of new callbacks after
shutdown. Passing an unpatched GUI_App.cpp as an argument exercises the control:
the queued callback produces an AddressSanitizer heap-use-after-free.

This is an adapter-level regression, not a full wxWidgets event-loop test or a
reproduction of a proprietary SDK worker-thread crash. It does not prove that
plugin destruction joins every internal worker, or make callback object
destruction safe after unloading the plugin. Full application and logged-in
network shutdown testing are still required.
