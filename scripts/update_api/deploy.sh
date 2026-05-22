#!/bin/bash
# Deploy slicer-update-api on apt.s3-dev.ovh
# Run as root on the server:  bash deploy.sh
set -e

DEPLOY_DIR="/opt/slicer-update-api"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NPM_NETWORK="npm_default"

echo "==> Creating deploy directory..."
mkdir -p "$DEPLOY_DIR"
cp "$SCRIPT_DIR/slicer_update.php" "$DEPLOY_DIR/slicer_update.php"

echo "==> Writing docker-compose.yml..."
cat > "$DEPLOY_DIR/docker-compose.yml" << 'EOF'
version: "3"
services:
  slicer-update-api:
    image: php:8.2-cli
    container_name: slicer-update-api
    restart: unless-stopped
    working_dir: /app
    command: php -S 0.0.0.0:8080 slicer_update.php
    volumes:
      - ./slicer_update.php:/app/slicer_update.php:ro
    networks:
      - npm_net

networks:
  npm_net:
    external: true
    name: npm_default
EOF

echo "==> Starting container..."
cd "$DEPLOY_DIR"
docker compose up -d --pull always

echo "==> Waiting for container to be ready..."
sleep 3
docker exec slicer-update-api php -r 'echo "PHP OK\n";'

echo "==> Configuring NPM route via API..."

# Get NPM admin token
NPM_TOKEN=$(curl -sf -X POST http://localhost:81/api/tokens \
  -H "Content-Type: application/json" \
  -d '{"identity":"admin@example.com","secret":"changeme"}' \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])" 2>/dev/null || true)

if [ -z "$NPM_TOKEN" ]; then
  echo ""
  echo "!!! Konnte NPM-Token nicht holen — NPM-Admin-Passwort ist nicht 'changeme'."
  echo "    Bitte manuell in NPM-UI (https://apt.s3-dev.ovh:81) konfigurieren:"
  echo ""
  echo "    Proxy Host fuer apt.s3-dev.ovh öffnen → Advanced → folgendes einfügen:"
  echo ""
  cat << 'NGINX'
    location /v1/iot-service/api/slicer/resource {
        proxy_pass http://slicer-update-api:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
NGINX
  echo ""
  echo "Container läuft bereits — nur noch die NPM-Route fehlt."
  exit 0
fi

echo "==> NPM-Token erhalten, suche Proxy-Host fuer apt.s3-dev.ovh..."

# Find proxy host ID for apt.s3-dev.ovh
HOST_ID=$(curl -sf http://localhost:81/api/nginx/proxy-hosts \
  -H "Authorization: Bearer $NPM_TOKEN" \
  | python3 -c "
import sys, json
hosts = json.load(sys.stdin)
for h in hosts:
    names = h.get('domain_names', [])
    if any('apt.s3-dev.ovh' in n for n in names):
        print(h['id'])
        break
" 2>/dev/null || true)

if [ -z "$HOST_ID" ]; then
  echo "!!! Proxy-Host fuer apt.s3-dev.ovh nicht gefunden."
  echo "    Bitte manuell in NPM konfigurieren (siehe oben)."
  exit 0
fi

echo "==> Proxy-Host ID: $HOST_ID — füge location block hinzu..."

# Get existing advanced config
EXISTING=$(curl -sf "http://localhost:81/api/nginx/proxy-hosts/$HOST_ID" \
  -H "Authorization: Bearer $NPM_TOKEN" \
  | python3 -c "import sys,json; print(json.load(sys.stdin).get('advanced_config',''))" 2>/dev/null || true)

LOCATION_BLOCK='
location /v1/iot-service/api/slicer/resource {
    proxy_pass http://slicer-update-api:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}'

if echo "$EXISTING" | grep -q "slicer/resource"; then
  echo "==> Location block bereits vorhanden — kein Update nötig."
else
  NEW_CONFIG="${EXISTING}${LOCATION_BLOCK}"
  curl -sf -X PUT "http://localhost:81/api/nginx/proxy-hosts/$HOST_ID" \
    -H "Authorization: Bearer $NPM_TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"advanced_config\": $(echo "$NEW_CONFIG" | python3 -c "import sys,json; print(json.dumps(sys.stdin.read()))")}" \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('OK' if 'id' in d else d)"
  echo "==> NPM-Route gesetzt."
fi

echo ""
echo "=== Deploy abgeschlossen ==="
echo "Test: curl 'https://apt.s3-dev.ovh/v1/iot-service/api/slicer/resource?distro=debian&arch=x86_64&slicer=01.10.00.00'"
