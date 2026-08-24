/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **
 ** NetMetrics.h
 **
 ** Container central e thread-safe para a telemetria de netplay em tempo real.
 ** Escrito pelas threads do netcode (envio/recepcao/loop de frame) e lido pela
 ** thread de render (HUD). Todos os campos sao atomicos, entao nao precisa de
 ** mutex para atualizar/ler valores simples.
 **
 ** Uso rapido:
 **   // no netcode, ao medir o RTT:
 **   NetMetrics::Get().UpdatePingSample(rttMs);
 **   // no buffer adaptativo, a cada frame:
 **   NetMetrics::Get().SetInputDelayFrames(inputBuffer.GetDelayFrames());
 **   // no rollback, quando re-simular:
 **   NetMetrics::Get().ReportRollback(framesResimulados);
 **   // no HUD:
 **   double ping = NetMetrics::Get().GetPingMs();
 **/

#ifndef INCLUDED_NETMETRICS_H
#define INCLUDED_NETMETRICS_H

#include <atomic>
#include <cstdint>

class NetMetrics
{
public:
    // Singleton global (Meyers). Seguro em C++11+.
    static NetMetrics& Get()
    {
        static NetMetrics instance;
        return instance;
    }

    // -----------------------------------------------------------------
    //  PING / RTT (em milissegundos)
    // -----------------------------------------------------------------
    // Define o ping diretamente (valor ja calculado/suavizado).
    void SetPingMs(double ms)
    {
        m_pingMs.store(ms, std::memory_order_relaxed);
    }

    // Leitura para o HUD.
    double GetPingMs() const
    {
        return m_pingMs.load(std::memory_order_relaxed);
    }

    // Alimenta uma amostra crua de RTT e mantem uma media movel exponencial
    // (EMA) para o numero nao ficar "pulando" na tela. alpha entre 0 e 1:
    // quanto menor, mais suave (e mais lento pra reagir).
    void UpdatePingSample(double sampleMs, double alpha = 0.2)
    {
        if (sampleMs < 0.0)
            return;
        const double prev = m_pingMs.load(std::memory_order_relaxed);
        const double next = (prev <= 0.0)
                          ? sampleMs
                          : (prev * (1.0 - alpha) + sampleMs * alpha);
        m_pingMs.store(next, std::memory_order_relaxed);

        // guarda tambem o pico, util pra debugar jitter
        const double peak = m_pingPeakMs.load(std::memory_order_relaxed);
        if (sampleMs > peak)
            m_pingPeakMs.store(sampleMs, std::memory_order_relaxed);
    }

    double GetPingPeakMs() const
    {
        return m_pingPeakMs.load(std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    //  INPUT DELAY (frames aplicados pelo buffer adaptativo)
    // -----------------------------------------------------------------
    void SetInputDelayFrames(int frames)
    {
        m_inputDelay.store(frames, std::memory_order_relaxed);
    }

    int GetInputDelayFrames() const
    {
        return m_inputDelay.load(std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    //  ROLLBACK (frames re-simulados)
    // -----------------------------------------------------------------
    // Chame quando ocorrer um rollback, passando quantos frames foram
    // re-simulados nesta correcao. Atualiza tambem o pico.
    void ReportRollback(int framesResimulated)
    {
        m_rollbackFrames.store(framesResimulated, std::memory_order_relaxed);
        const int peak = m_maxRollback.load(std::memory_order_relaxed);
        if (framesResimulated > peak)
            m_maxRollback.store(framesResimulated, std::memory_order_relaxed);
    }

    // Define diretamente (ex.: 0 quando nao houve rollback no frame).
    void SetRollbackFrames(int frames)
    {
        m_rollbackFrames.store(frames, std::memory_order_relaxed);
    }

    int GetRollbackFrames() const
    {
        return m_rollbackFrames.load(std::memory_order_relaxed);
    }

    int GetMaxRollbackFrames() const
    {
        return m_maxRollback.load(std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    //  ESTADO DE CONEXAO
    // -----------------------------------------------------------------
    void SetConnected(bool connected)
    {
        m_connected.store(connected, std::memory_order_relaxed);
    }

    bool IsConnected() const
    {
        return m_connected.load(std::memory_order_relaxed);
    }

    // Zera tudo (chamar em Reset() do netboard).
    void Reset()
    {
        m_pingMs.store(0.0, std::memory_order_relaxed);
        m_pingPeakMs.store(0.0, std::memory_order_relaxed);
        m_inputDelay.store(0, std::memory_order_relaxed);
        m_rollbackFrames.store(0, std::memory_order_relaxed);
        m_maxRollback.store(0, std::memory_order_relaxed);
        m_connected.store(false, std::memory_order_relaxed);
    }

private:
    NetMetrics() = default;
    NetMetrics(const NetMetrics&) = delete;
    NetMetrics& operator=(const NetMetrics&) = delete;

    std::atomic<double> m_pingMs{0.0};
    std::atomic<double> m_pingPeakMs{0.0};
    std::atomic<int>    m_inputDelay{0};
    std::atomic<int>    m_rollbackFrames{0};
    std::atomic<int>    m_maxRollback{0};
    std::atomic<bool>   m_connected{false};
};

#endif // INCLUDED_NETMETRICS_H
