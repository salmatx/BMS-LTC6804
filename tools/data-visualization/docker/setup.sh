#!/bin/bash
set -euo pipefail

# Generate token
INFLUX_TOKEN=$(openssl rand -hex 32)

echo "Creating directories..."
mkdir -p \
  influxdb2-data \
  influxdb2-config \
  grafana-data \
  mosquitto/config \
  mosquitto/data \
  mosquitto/log

echo "Setting Grafana permissions..."
sudo chown 472:472 ./grafana-data

# Write .env
cat > .env << EOF
INFLUX_TOKEN=${INFLUX_TOKEN}
EOF

echo ""
echo "=== Setup complete ==="
echo "Your token: ${INFLUX_TOKEN}"
echo ""
echo "Run: docker compose up -d"
