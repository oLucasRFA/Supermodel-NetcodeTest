from pathlib import Path

path = Path("Src/Network/SimNetBoard.cpp")
data = path.read_bytes()

# 1. Adicionar estado nas duas entradas de State::ready
old = b"\t\t\tm_state = State::ready;\r\n"
new = (
    b"\t\t\tm_pendingSegment = 0;\r\n"
    b"\t\t\tm_waitingForPacket = false;\r\n"
    b"\t\t\tm_state = State::ready;\r\n"
)
count = data.count(old)
if count != 2:
    raise SystemExit(f"ERRO: entradas em State::ready encontradas: {count}.")
data = data.replace(old, new)

# 2. Substituir o loop bloqueante
old_loop = (
    b"\t\t// we only send what we need to; helps cut down on bandwidth\r\n"
    b"\t\t// each machine has to receive back its own data (TODO: copy this data manually?)\r\n"
    b"\t\tfor (int i = 0; i < m_numMachines; i++)\r\n"
    b"\t\t{\r\n"
    b"\t\t\tnets->Send(CommRAM + 0x100 + i * m_segmentSize, m_segmentSize);\r\n"
    b"\t\t\tauto& recv_data = netr->Receive();\r\n"
    b"\t\t\tif (recv_data.empty())\r\n"
    b"\t\t\t{\r\n"
    b"\t\t\t\t// link broken - send an \"empty\" packet to alert other machines\r\n"
    b"\t\t\t\tnets->Send(nullptr, 0);\r\n"
    b"\t\t\t\tm_state = State::error;\r\n"
    b"\t\t\t\tif (m_gameType == GameType::one)\r\n"
    b"\t\t\t\t\tm_status1 = 0x40;\t\t\t// send \"link broken\" message to mainboard\r\n"
    b"\t\t\t\tbreak;\r\n"
    b"\t\t\t}\r\n"
    b"\t\t\tmemcpy(CommRAM + 0x100 + (i + 1) * m_segmentSize, recv_data.data(), recv_data.size());\r\n"
    b"\t\t}\r\n"
)

new_loop = (
    b"\t\t// Exchange one segment at a time without blocking the emulation thread.\r\n"
    b"\t\tif (!m_waitingForPacket)\r\n"
    b"\t\t{\r\n"
    b"\t\t\tnets->Send(CommRAM + 0x100 + m_pendingSegment * m_segmentSize, m_segmentSize);\r\n"
    b"\t\t\tm_waitingForPacket = true;\r\n"
    b"\t\t\tbreak;\r\n"
    b"\t\t}\r\n"
    b"\r\n"
    b"\t\tstd::vector<char> recv_data;\r\n"
    b"\t\tif (!netr->TryReceive(recv_data))\r\n"
    b"\t\t\tbreak;\r\n"
    b"\r\n"
    b"\t\tif (recv_data.empty())\r\n"
    b"\t\t{\r\n"
    b"\t\t\t// Link broken - send an empty packet to alert other machines.\r\n"
    b"\t\t\tnets->Send(nullptr, 0);\r\n"
    b"\t\t\tm_state = State::error;\r\n"
    b"\t\t\tm_waitingForPacket = false;\r\n"
    b"\t\t\tif (m_gameType == GameType::one)\r\n"
    b"\t\t\t\tm_status1 = 0x40;\r\n"
    b"\t\t\tbreak;\r\n"
    b"\t\t}\r\n"
    b"\r\n"
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
    b"\t\tm_waitingForPacket = false;\r\n"
    b"\r\n"
    b"\t\tif (m_pendingSegment < m_numMachines)\r\n"
    b"\t\t\tbreak;\r\n"
)

count_loop = data.count(old_loop)
if count_loop != 1:
    raise SystemExit(f"ERRO: loop bloqueante encontrado {count_loop} vezes.")
data = data.replace(old_loop, new_loop, 1)

# 3. Substituir o bloco de troca de bancos
old_swap = (
    b"\t\t// swap CommRAM banks\r\n"
    b"\t\tif (m_commbank)\r\n"
    b"\t\t{\r\n"
    b"\t\t\tm_commbank = false;\r\n"
    b"\t\t\tCommRAM = Buffer;\r\n"
    b"\t\t\texternalCommRAM = Buffer + 0x10000;\r\n"
    b"\t\t}\r\n"
    b"\t\telse\r\n"
    b"\t\t{\r\n"
    b"\t\t\tm_commbank = true;\r\n"
    b"\t\t\tCommRAM = Buffer + 0x10000;\r\n"
    b"\t\t\texternalCommRAM = Buffer;\r\n"
    b"\t\t}\r\n"
)

new_swap = (
    b"\t\t// Swap CommRAM banks only after all segments have been received.\r\n"
    b"\t\tif (m_pendingSegment == m_numMachines)\r\n"
    b"\t\t{\r\n"
    b"\t\t\tif (m_commbank)\r\n"
    b"\t\t\t{\r\n"
    b"\t\t\t\tm_commbank = false;\r\n"
    b"\t\t\t\tCommRAM = Buffer;\r\n"
    b"\t\t\t\texternalCommRAM = Buffer + 0x10000;\r\n"
    b"\t\t\t}\r\n"
    b"\t\t\telse\r\n"
    b"\t\t\t{\r\n"
    b"\t\t\t\tm_commbank = true;\r\n"
    b"\t\t\t\tCommRAM = Buffer + 0x10000;\r\n"
    b"\t\t\t\texternalCommRAM = Buffer;\r\n"
    b"\t\t\t}\r\n"
    b"\r\n"
    b"\t\t\tm_pendingSegment = 0;\r\n"
    b"\t\t}\r\n"
)

count_swap = data.count(old_swap)
if count_swap != 1:
    raise SystemExit(f"ERRO: bloco de troca encontrado {count_swap} vezes.")
data = data.replace(old_swap, new_swap, 1)

# 4. Adicionar { } em torno de case State::ready
old_case = b"\tcase State::ready:\r\n\t\tm_counter++;\r\n\t\tCommRAM16[0x6] = FLIPENDIAN16(m_counter);\r\n\t\t\r\n\t\t// Exchange one segment at a time without blocking the emulation thread.\r\n"
new_case = b"\tcase State::ready:\r\n\t{\r\n\t\tm_counter++;\r\n\t\tCommRAM16[0x6] = FLIPENDIAN16(m_counter);\r\n\t\t\r\n\t\t// Exchange one segment at a time without blocking the emulation thread.\r\n"

count_case = data.count(old_case)
if count_case != 1:
    raise SystemExit(f"ERRO: case State::ready encontrado {count_case} vezes.")
data = data.replace(old_case, new_case, 1)

# 5. Fechar o bloco antes de case State::error
old_end = b"\t\t\tm_pendingSegment = 0;\r\n\t\t}\r\n\r\n\t\tbreak;\r\n\r\n\tcase State::error:\r\n\t\t// do nothing\r\n"
new_end = b"\t\t\tm_pendingSegment = 0;\r\n\t\t}\r\n\r\n\t\tbreak;\r\n\t}\r\n\r\n\tcase State::error:\r\n\t\t// do nothing\r\n"

count_end = data.count(old_end)
if count_end != 1:
    raise SystemExit(f"ERRO: fim de State::ready encontrado {count_end} vezes.")
data = data.replace(old_end, new_end, 1)

path.write_bytes(data)
print("OK: todas as alterações aplicadas com instrumentação.")
