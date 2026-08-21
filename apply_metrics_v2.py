from pathlib import Path

path = Path("Src/Network/SimNetBoard.cpp")
data = path.read_bytes()

# Inserir logging após o memcpy no loop assíncrono
old = (
    b"\t\tmemcpy(CommRAM + 0x100 + (m_pendingSegment + 1) * m_segmentSize,\r\n"
    b"\t\t\trecv_data.data(), recv_data.size());\r\n"
    b"\r\n"
    b"\t\tm_pendingSegment++;\r\n"
)

new = (
    b"\t\tmemcpy(CommRAM + 0x100 + (m_pendingSegment + 1) * m_segmentSize,\r\n"
    b"\t\t\trecv_data.data(), recv_data.size());\r\n"
    b"\r\n"
    b"\t\t// Log metrics for analysis\r\n"
    b"\t\tstatic uint64_t total_recv = 0;\r\n"
    b"\t\tstatic uint64_t total_latency_us = 0;\r\n"
    b"\t\tstatic uint64_t max_latency_us = 0;\r\n"
    b"\t\tstatic uint64_t segment_start_tick = 0;\r\n"
    b"\t\tstatic uint64_t segment_count = 0;\r\n"
    b"\t\t{\r\n"
    b"\t\t\tauto now = std::chrono::steady_clock::now();\r\n"
    b"\t\t\tif (segment_start_tick == 0) segment_start_tick = now.time_since_epoch().count();\r\n"
    b"\t\t\tauto elapsed_us = (now.time_since_epoch().count() - segment_start_tick) / 1000;\r\n"
    b"\t\t\ttotal_recv++;\r\n"
    b"\t\t\ttotal_latency_us += elapsed_us;\r\n"
    b"\t\t\tif (elapsed_us > max_latency_us) max_latency_us = elapsed_us;\r\n"
    b"\t\t\tsegment_count++;\r\n"
    b"\t\t\tif (segment_count % 60 == 0)\r\n"
    b"\t\t\t{\r\n"
    b"\t\t\t\tfprintf(stderr, \"[NetMetrics] recv_count=%lu avg_us=%lu max_us=%lu segment=%d machines=%d\\n\",\r\n"
    b"\t\t\t\t\ttotal_recv, total_latency_us / total_recv, max_latency_us, m_segmentSize, m_numMachines);\r\n"
    b"\t\t\t\tfflush(stderr);\r\n"
    b"\t\t\t}\r\n"
    b"\t\t}\r\n"
    b"\r\n"
    b"\t\tm_pendingSegment++;\r\n"
)

count = data.count(old)
if count != 1:
    raise SystemExit(f"ERRO: ponto de inserção encontrado {count} vezes.")

data = data.replace(old, new, 1)
path.write_bytes(data)
print("OK: instrumentação aplicada.")
