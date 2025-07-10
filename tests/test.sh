#!/usr/bin/env bash
set -euo pipefail

#fecha qualquer processo na porta 8080
cleanup_port() {
    if command -v lsof &>/dev/null && lsof -ti tcp:8080 &>/dev/null; then
        echo "[test] limpando qualquer processo na porta 8080"
        lsof -ti tcp:8080 | xargs kill -9 || true
        sleep 1
    elif command -v fuser &>/dev/null; then
        echo "[test] limpando qualquer processo na porta 8080"
        fuser -k 8080/tcp || true
        sleep 1
    fi
}

cleanup_port

#compila tudo
echo "[test] compilando servidor e cliente"
make -s server
make -s client

#inicia o servidor em backgound
echo "[test] iniciando servidor..."
rm -f tests/server.log
./server/battleserver > tests/server.log 2>&1 &
SERVER_PID=$!
sleep 1

#prepara os comandos se não existirem
cat > tests/client1_commands.txt << 'EOF'
JOIN P1
POS DESTROYER 1 1 H
POS FRAGATA 2 1 H
POS FRAGATA 3 1 H
POS SUBMARINO 4 1 H
READY
FIRE 1 1
FIRE 1 2
FIRE 1 3
FIRE 2 1
FIRE 2 2
FIRE 3 1
FIRE 3 2
FIRE 4 1
EOF

cat > tests/client2_commands.txt << 'EOF'
JOIN P2
POS DESTROYER 1 1 H
POS FRAGATA 2 1 H
POS FRAGATA 3 1 H
POS SUBMARINO 4 1 H
READY
FIRE 1 1
FIRE 1 2
FIRE 1 3
FIRE 2 1
FIRE 2 2
FIRE 3 1
FIRE 3 2
FIRE 4 1
EOF

# inicia os dois clientes, fazendo o passeio por 1 segundo entre as linhas
echo "[test] iniciando cliente 1..."
> tests/client1.log
(
  while IFS= read -r cmd; do
    echo "$cmd"
    sleep 1
  done < tests/client1_commands.txt
) | timeout 60 ./client/battleclient > tests/client1.log 2>&1 &
CLIENT1_PID=$!

echo "[test] iniciando cliente 2..."
> tests/client2.log
(
  while IFS= read -r cmd; do
    echo "$cmd"
    sleep 1
  done < tests/client2_commands.txt
) | timeout 60 ./client/battleclient > tests/client2.log 2>&1 &
CLIENT2_PID=$!

#espera ambos os clientes terminarem
echo "[test] aguardando clientes terminarem..."
wait $CLIENT1_PID
wait $CLIENT2_PID
echo "[test] clientes finalizaram"

# para o servidor silenciando erro se já saiu
kill $SERVER_PID 2>/dev/null || true

#verifica a vitoria/derrota nos logs
echo "[test] analisando resultados..."
if grep -E "WINS|GANHOU|VENCEU" tests/client1.log \
   && grep -E "LOSES|PERDEU" tests/client2.log; then
    echo "[test] ✓ Player 1 venceu como esperado"
elif grep -E "WINS|GANHOU|VENCEU" tests/client2.log \
   && grep -E "LOSES|PERDEU" tests/client1.log; then
    echo "[test] ✓ Player 2 venceu como esperado"
else
    echo "[test] ✗ Falha no resultado esperado"
    echo "=== LOGS DE DEBUG ==="
    echo "---- servidor ----"
    cat tests/server.log
    echo "---- client1.log ----"
    cat tests/client1.log
    echo "---- client2.log ----"
    cat tests/client2.log
    exit 1
fi

echo "[test] todos os testes passaram!"
