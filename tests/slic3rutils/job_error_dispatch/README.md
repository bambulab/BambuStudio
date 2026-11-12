# Job error dispatch regression

This standalone executable compiles the real `src/slic3r/GUI/Jobs/Job.cpp` and
uses a recording `ProgressIndicator` with a console wx application. It creates
no native windows and does not initialize Studio, load user settings, use the
network, or connect to a printer. It does not call `Job::start()` or change a
busy cursor.

It verifies that errors submitted from a worker remain deferred until the main
thread pumps wx events, retain all payload fields (including Unicode and an
embedded NUL), and preserve FIFO order relative to callbacks on the same Job.
It also verifies cancellation suppression and destruction of a Job with a
pending callback, including release of the retained progress indicator.

Configure this directory separately from the application build. A previously
configured GUI build supplies the generated version header; configuring this
test does not regenerate that header. For the local macOS dependency prefix:

```sh
cmake -S tests/slic3rutils/job_error_dispatch -B /tmp/bambu-job-error-dispatch \
  -DCMAKE_PREFIX_PATH=/tmp/bambu-height-official-deps \
  -DwxWidgets_CONFIG_EXECUTABLE=/tmp/bambu-height-official-deps/bin/wx-config \
  -DwxWidgets_CONFIG_OPTIONS=--prefix=/tmp/bambu-height-official-deps \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/bambu-job-error-dispatch -j 2
ctest --test-dir /tmp/bambu-job-error-dispatch --output-on-failure
```

Set `BAMBU_GUI_BUILD_DIR` if the configured application build is somewhere
other than `build/gui-check`. The test links wx core because `Job.cpp` contains
GUI cursor calls, but initializes only `wxAppConsole` and does not invoke those
methods. The test checks the shared Print/Send job dispatch mechanism; actual
printer upload success and native dialog appearance are separate checks.
