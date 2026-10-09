#!/usr/bin/env bash
set -euo pipefail

# Diretório raiz do projeto
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${TRAMA_DATA_DIR:-$ROOT/var}"
HOST="${TRAMA_HOST:-0.0.0.0}"
PREFERRED_PORT="${TRAMA_PORT:-${PORT:-8088}}"

echo "=========================================================="
echo "  Iniciando TRAMA..."
echo "=========================================================="

# 1. Compilação automática caso necessário
if [[ ! -x "$ROOT/build/bin/trama-server" || ! -x "$ROOT/build/bin/trama" || ! -f "$ROOT/web/dist/index.html" ]]; then
  echo "[-] Binários ou frontend ausentes. Compilando o projeto..."
  "$ROOT/scripts/build.sh"
fi

# 2. Inicialização da base de dados e diretórios
mkdir -p "$DATA_DIR"
if [[ ! -f "$DATA_DIR/trama.sqlite3" ]]; then
  echo "[-] Inicializando base de dados em $DATA_DIR..."
  "$ROOT/build/bin/trama" init --data-dir "$DATA_DIR"
fi

# 3. Bootstrap de administrador se nenhum existir
ADMIN_COUNT=$(sqlite3 "$DATA_DIR/trama.sqlite3" "SELECT count(*) FROM users WHERE system_role='admin' AND active=1;" 2>/dev/null || echo "0")
ADMIN_CREATED=0
if [[ "$ADMIN_COUNT" -eq 0 ]]; then
  ADMIN_LOGIN="${TRAMA_ADMIN_LOGIN:-admin}"
  ADMIN_PASS="${TRAMA_ADMIN_PASSWORD:-admin12345678}"
  echo "[-] Nenhum administrador ativo encontrado. Criando usuário '$ADMIN_LOGIN'..."
  "$ROOT/build/bin/trama" setup-admin --data-dir "$DATA_DIR" --login "$ADMIN_LOGIN" --password "$ADMIN_PASS"
  ADMIN_CREATED=1
fi

# 4. Encontrar porta livre
find_free_port() {
  local start_port="$1"
  python3 -c "
import socket
import sys

def is_free(p):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('0.0.0.0', p))
        s.close()
        return True
    except OSError:
        return False

port = int(sys.argv[1])
while port < 65535:
    if is_free(port):
        print(port)
        sys.exit(0)
    port += 1
sys.exit(1)
" "$start_port" 2>/dev/null || echo "$start_port"
}

SELECTED_PORT=$(find_free_port "$PREFERRED_PORT")

if [[ "$SELECTED_PORT" != "$PREFERRED_PORT" ]]; then
  echo "[!] Porta $PREFERRED_PORT em uso. Selecionada porta livre: $SELECTED_PORT"
fi

# 5. Descobrir endereços IP da máquina
get_ips() {
  local ips=""
  if command -v hostname >/dev/null 2>&1; then
    ips=$(hostname -I 2>/dev/null || true)
  fi
  if [[ -z "$ips" ]] && command -v ip >/dev/null 2>&1; then
    ips=$(ip -4 addr show | awk '/inet / && !/127\.0\.0\.1/ {print $2}' | cut -d/ -f1 | tr '\n' ' ')
  fi
  echo "$ips"
}

LAN_IPS=$(get_ips)

# 6. Banner informativo
echo ""
echo "=========================================================="
echo "  🚀 TRAMA Servidor Pronto!"
echo "=========================================================="
echo "  Host de escuta: $HOST (Rede Local e Loopback)"
echo "  Porta:          $SELECTED_PORT"
echo "  Base de dados:  $DATA_DIR/trama.sqlite3"
echo ""
echo "  🌐 Acesse no navegador:"
echo "     👉 Local:      http://localhost:$SELECTED_PORT"
echo "     👉 Loopback:   http://127.0.0.1:$SELECTED_PORT"

for ip in $LAN_IPS; do
  if [[ "$ip" != "127.0.0.1" ]]; then
    echo "     👉 Rede Local: http://$ip:$SELECTED_PORT"
  fi
done

if [[ "$ADMIN_CREATED" -eq 1 ]]; then
  echo ""
  echo "  🔑 Credenciais de Administrador:"
  echo "     Usuário: $ADMIN_LOGIN"
  echo "     Senha:   $ADMIN_PASS"
fi

echo "=========================================================="
echo "  Pressione Ctrl+C para parar o servidor."
echo "=========================================================="
echo ""

# 7. Execução do servidor
exec "$ROOT/build/bin/trama-server" \
  --source-dir "$ROOT" \
  --data-dir "$DATA_DIR" \
  --host "$HOST" \
  --port "$SELECTED_PORT" \
  "$@"
