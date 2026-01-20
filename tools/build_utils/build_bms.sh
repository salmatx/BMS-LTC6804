#!/bin/bash
set -e

# Init ESP-IDF env
. /opt/esp-idf/export.sh

# Go to project dir
cd "../../src"

mkdir -p build

# Clean build directory (true condition added to don't fail when build is empty before clean)
idf.py fullclean || true

# Run build, log stdout+stderr
idf.py reconfigure build |& tee build/build_log.txt

