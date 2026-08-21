#!/usr/bin/env bash
set -e

cd /home/deck/Projects/supermodel-netplay

# Limpar logs antigos
rm -f dist/Player1/Data/Supermodel.log dist/Player2/Data/Supermodel.log
rm -f dist/Player1/Data/Logs/launch.log dist/Player2/Data/Logs/launch.log

# Iniciar Player 1 e Player 2 em janelas separadas
konsole -e bash -c "
cd /home/deck/Projects/supermodel-netplay
./dist/Player1/run-player1.sh
" &
PID1=$!

konsole -e bash -c "
cd /home/deck/Projects/supermodel-netplay
./dist/Player2/run-player2.sh
" &
PID2=$!

echo "Player 1 PID: $PID1"
echo "Player 2 PID: $PID2"
echo "Aguardando... pressione Ctrl+C para parar e extrair logs."

wait $PID1 $PID2 || true

# Extrair métricas
echo ""
echo "=== Player 1 NetMetrics ==="
grep '\[NetMetrics\]' dist/Player1/Data/Logs/launch.log 2>/dev/null | tail -20 || echo "Sem logs"

echo ""
echo "=== Player 2 NetMetrics ==="
grep '\[NetMetrics\]' dist/Player2/Data/Logs/launch.log 2>/dev/null | tail -20 || echo "Sem logs"
