#!/bin/sh
exec /workspace/suitecut/.suitecut/native-build/suitecut-browser --suitecut-diagnostics "$@" 2>>/workspace/suitecut/benchmarks/obs-cef/2026-10-09-dropout-investigation/controlled-node-stall/logs/native.log
