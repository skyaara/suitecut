#!/bin/sh
export LIBGL_ALWAYS_SOFTWARE=1
exec /workspace/suitecut/.suitecut/native-build/suitecut-browser --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader "$@"
