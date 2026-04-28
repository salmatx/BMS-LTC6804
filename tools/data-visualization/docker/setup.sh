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
sudo chown -R 472:472 ./grafana-data

# Write .env
cat > .env << EOF
INFLUX_TOKEN=${INFLUX_TOKEN}
EOF

echo "Creating Mosquitto config..."
cat > mosquitto/config/mosquitto.conf << 'EOF'
persistence true
persistence_location /mosquitto/data/
log_dest file /mosquitto/log/mosquitto.log
listener 1883
allow_anonymous true
EOF

echo "Setting Telegraf permissions..."
sudo chmod 644 telegraf/telegraf.conf

echo ""
echo "=== Setup complete ==="
echo "Your token: ${INFLUX_TOKEN}"
echo ""
echo "Run: docker compose up -d"
