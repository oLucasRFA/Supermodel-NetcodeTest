/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **
 ** RollbackManager.h
 **
 ** Motor de rollback DESACOPLADO do emulador. Ele nao conhece o CModel3:
 ** opera atraves de 3 callbacks que voce fornece uma unica vez.
 **
 **   SaveFn : serializa o estado ATUAL do emulador num blob de bytes.
 **   LoadFn : restaura o estado do emulador a partir de um blob.
 **   StepFn : avanca a emulacao EXATAMENTE 1 frame, aplicando os inputs
 **            (locais + remotos ja confirmados) daquele frame.
 **
 ** Semantica dos snapshots:
 **   snapshot[f] = estado do emulador APOS simular o frame f.
 **   Para re-simular a partir do frame C, restauramos snapshot[C-1] e
 **   avancamos C, C+1, ..., currentFrame novamente.
 **
 ** Integra com NetMetrics: cada rollback reporta quantos frames foram
 ** re-simulados (campo "RB" do HUD).
 **
 ** IMPORTANTE (custo de memoria): cada snapshot pode ter varios MB. O anel
 ** guarda no maximo 'maxRollbackFrames' snapshots. Mantenha esse numero
 ** pequeno (ex.: 8-10). Rollback so consegue voltar ate onde o anel alcanca.
 **
 ** REQUISITOS DE EXECUCAO:
 **   - Rodar com -no-threads (determinismo frame a frame).
 **   - StepFn deve ser deterministica (mesmo input => mesmo resultado).
 **   - Idealmente silenciar/descartar audio durante a re-simulacao.
 **/

#ifndef INCLUDED_ROLLBACKMANAGER_H
#define INCLUDED_ROLLBACKMANAGER_H

#include "NetMetrics.h"
#include <cstdint>
#include <vector>
#include <deque>
#include <functional>
#include <utility>

class RollbackManager
{
public:
    using SaveFn = std::function<void(std::vector<uint8_t>& outBlob)>;
    using LoadFn = std::function<void(const std::vector<uint8_t>& blob)>;
    using StepFn = std::function<void(uint32_t frame)>;

    // -----------------------------------------------------------------
    //  Configuracao
    // -----------------------------------------------------------------
    void SetCallbacks(SaveFn save, LoadFn load, StepFn step)
    {
        m_save = std::move(save);
        m_load = std::move(load);
        m_step = std::move(step);
    }

    // Profundidade maxima do anel de snapshots (quantos frames de historia).
    void Configure(uint32_t maxRollbackFrames)
    {
        m_maxFrames = (maxRollbackFrames < 1) ? 1 : maxRollbackFrames;
    }

    void Reset()
    {
        m_snapshots.clear();
        m_inRollback = false;
    }

    bool IsReady() const
    {
        return m_save && m_load && m_step;
    }

    // Evita rollback reentrante: durante a re-simulacao NAO capture/reconcilie.
    bool IsResimulating() const { return m_inRollback; }

    // -----------------------------------------------------------------
    //  CaptureSnapshot(frame)
    //  Chame DEPOIS de simular 'frame' (com input previsto ou confirmado).
    //  Guarda o estado no anel e descarta os mais antigos.
    // -----------------------------------------------------------------
    void CaptureSnapshot(uint32_t frame)
    {
        if (!m_save || m_inRollback)
            return;

        std::vector<uint8_t> blob;
        m_save(blob);
        if (blob.empty())
            return;

        // Se ja existe snapshot desse frame (recaptura pos-rollback), atualiza.
        for (auto& e : m_snapshots)
        {
            if (e.first == frame)
            {
                e.second = std::move(blob);
                return;
            }
        }

        m_snapshots.emplace_back(frame, std::move(blob));

        // Mantem o anel dentro do limite.
        while (m_snapshots.size() > m_maxFrames)
            m_snapshots.pop_front();
    }

    bool HasSnapshot(uint32_t frame) const
    {
        for (const auto& e : m_snapshots)
            if (e.first == frame)
                return true;
        return false;
    }

    // Quantos frames pra tras conseguimos reverter a partir de 'currentFrame'.
    bool CanRollbackTo(uint32_t frame) const
    {
        // Precisamos do estado do frame ANTERIOR ao primeiro a recalcular.
        if (frame == 0)
            return true;
        return HasSnapshot(frame - 1);
    }

    // -----------------------------------------------------------------
    //  Reconcile(firstWrongFrame, currentFrame)
    //  Chame quando o SimNetBoard confirmar um input que DIVERGE da
    //  predicao usada no frame 'firstWrongFrame'.
    //
    //  Restaura snapshot[firstWrongFrame-1] e re-simula ate currentFrame,
    //  recapturando os snapshots corrigidos no caminho.
    //
    //  Retorna: numero de frames re-simulados (>=1) em caso de sucesso;
    //           0 se nada a fazer; -1 se o snapshot base ja saiu do anel
    //           (rollback longo demais - a divergencia sera absorvida como
    //            erro de predicao, sem correcao).
    // -----------------------------------------------------------------
    int Reconcile(uint32_t firstWrongFrame, uint32_t currentFrame)
    {
        if (!IsReady() || m_inRollback)
            return 0;
        if (firstWrongFrame > currentFrame)
            return 0;

        const uint32_t baseFrame =
            (firstWrongFrame == 0) ? 0 : (firstWrongFrame - 1);

        const std::vector<uint8_t>* baseBlob = nullptr;
        if (firstWrongFrame != 0)
        {
            baseBlob = Find(baseFrame);
            if (baseBlob == nullptr)
                return -1; // base saiu do anel: nao da pra reverter tao longe
        }

        m_inRollback = true;

        // 1) Restaura o estado base (fim do frame anterior ao divergente).
        if (baseBlob != nullptr)
            m_load(*baseBlob);

        // 2) Re-simula do frame divergente ate o atual, recapturando.
        int resimulated = 0;
        for (uint32_t f = firstWrongFrame; f <= currentFrame; ++f)
        {
            m_step(f);

            std::vector<uint8_t> blob;
            m_save(blob);
            if (!blob.empty())
                StoreOrReplace(f, std::move(blob));

            ++resimulated;
        }

        m_inRollback = false;

        // 3) Reporta pro HUD (campo RB).
        NetMetrics::Get().ReportRollback(resimulated);

        return resimulated;
    }

private:
    const std::vector<uint8_t>* Find(uint32_t frame) const
    {
        for (const auto& e : m_snapshots)
            if (e.first == frame)
                return &e.second;
        return nullptr;
    }

    void StoreOrReplace(uint32_t frame, std::vector<uint8_t>&& blob)
    {
        for (auto& e : m_snapshots)
        {
            if (e.first == frame)
            {
                e.second = std::move(blob);
                return;
            }
        }
        m_snapshots.emplace_back(frame, std::move(blob));
        while (m_snapshots.size() > m_maxFrames)
            m_snapshots.pop_front();
    }

    SaveFn m_save;
    LoadFn m_load;
    StepFn m_step;

    uint32_t m_maxFrames = 10;
    bool     m_inRollback = false;

    // Anel simples: par (frame, blob). Ordenado por insercao.
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> m_snapshots;
};

#endif // INCLUDED_ROLLBACKMANAGER_H
