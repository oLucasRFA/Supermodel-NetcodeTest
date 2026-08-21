from pathlib import Path
import re
import shutil

path = Path("Src/Network/SimNetBoard.h")
data = path.read_bytes()

if b"m_networkMetricsEnabled" in data:
    print("AVISO: métricas já existem no header. Nenhuma alteração feita.")
    raise SystemExit(0)

match = re.search(
    rb"(?m)^([ \t]*uint16_t[ \t]+m_segmentSize[ \t]*=[ \t]*0;)(\r?\n)",
    data,
)

if not match:
    raise SystemExit("ERRO: m_segmentSize não encontrada.")

eol = match.group(2)

lines = [
    "",
    "   // Metricas experimentais de bloqueio da NetBoard.",
    "   bool m_networkMetricsEnabled = false;",
    "   uint64_t m_networkFrameCounter = 0;",
    "   uint64_t m_networkPacketsReceived = 0;",
    "   uint64_t m_networkReceiveTotalUS = 0;",
    "   uint64_t m_networkReceiveMaxUS = 0;",
    "   uint64_t m_networkFrameTotalUS = 0;",
    "   uint64_t m_networkFrameMaxUS = 0;",
    "   uint64_t m_networkSlowFrames = 0;",
]

block = match.group(1) + eol + eol.join(
    line.encode("ascii") for line in lines
) + eol

backup = path.with_suffix(".h.bak")
shutil.copy2(path, backup)

new_data = data[:match.start()] + block + data[match.end():]
path.write_bytes(new_data)

print(f"Backup criado: {backup}")
print("Campos de métricas adicionados sem alterar line endings.")
