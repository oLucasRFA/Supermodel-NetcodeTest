from pathlib import Path

cpp_path = Path("Src/Network/SimNetBoard.cpp")
text = cpp_path.read_text()

if "#include <chrono>" not in text:
    include = "#include <thread>"
    if include not in text:
        raise SystemExit("ERRO: include <thread> não encontrado.")
    text = text.replace(include, include + "\n#include <chrono>", 1)

runframe = "void CSimNetBoard::RunFrame(void)\n{"
if "using NetMetricsClock" not in text:
    if runframe not in text:
        raise SystemExit("ERRO: RunFrame não encontrado.")
    text = text.replace(
        runframe,
        runframe + """
   using NetMetricsClock = std::chrono::steady_clock;
   static uint64_t metricsFrameCount = 0;
   static uint64_t metricsReceiveTotalUS = 0;
   static uint64_t metricsReceiveMaxUS = 0;
""",
        1
    )

ready_receive = """            auto& recv_data = netr->Receive();"""

if "metricsReceiveStart" not in text:
    if ready_receive not in text:
        raise SystemExit("ERRO: Receive do State::ready não encontrado.")

    # Esse trecho aparece apenas uma vez como auto& no State::ready
    replacement = """            const auto metricsReceiveStart = NetMetricsClock::now();
            auto& recv_data = netr->Receive();
            const auto metricsReceiveEnd = NetMetricsClock::now();

            const uint64_t metricsReceiveUS =
                static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        metricsReceiveEnd - metricsReceiveStart
                    ).count()
                );

            ++metricsFrameCount;
            metricsReceiveTotalUS += metricsReceiveUS;

            if (metricsReceiveUS > metricsReceiveMaxUS)
                metricsReceiveMaxUS = metricsReceiveUS;

            if ((metricsFrameCount % 60) == 0)
            {
                InfoLog(
                    "[NetMetrics] frames=%llu recv_us_avg=%llu recv_us_max=%llu segment=%u",
                    static_cast<unsigned long long>(metricsFrameCount),
                    static_cast<unsigned long long>(
                        metricsReceiveTotalUS / metricsFrameCount
                    ),
                    static_cast<unsigned long long>(metricsReceiveMaxUS),
                    static_cast<unsigned>(m_segmentSize)
                );
            }"""

    # Para ter certeza de pegar o ready, substitui a ÚLTIMA ocorrência.
    position = text.rfind(ready_receive)
    if position < 0:
        raise SystemExit("ERRO: Receive do State::ready não localizado.")

    text = (
        text[:position]
        + replacement
        + text[position + len(ready_receive):]
    )

cpp_path.write_text(text)
print("Instrumentação mínima aplicada.")
