#! /bin/bash
set -e

# Init ESP-IDF env
. /opt/esp-idf/export.sh

# Go to project dir
cd "../../src"


idf.py flash monitor
