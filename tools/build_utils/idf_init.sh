#!/bin/bash

OLDPWD="$(pwd)"

cd /opt/esp-idf

. ./export.sh

cd "$OLDPWD"
