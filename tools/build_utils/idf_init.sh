#!/bin/bash
set -e

OLDPWD="$(pwd)"

cd /opt/esp-idf

. ./export.sh

cd "$OLDPWD"
