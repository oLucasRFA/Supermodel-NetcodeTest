/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **
 ** NetHUD.h
 **
 ** Indicador de netplay na BARRA DE TITULO da janela.
 ** Le os valores de NetMetrics (thread-safe) e monta uma string tipo:
 **
 **   Supermodel - Sega Rally 2 | PING: 34ms | IN: 3f | RB: 0f
 **
 ** Header-only, sem dependencia de SDL. O throttle interno evita atualizar
 ** o titulo todo frame (o que causa flicker e custa syscalls).
 **
 ** Uso no loop principal (Main.cpp):
 **   if (netEnabled && NetHUD::ShouldUpdate())
 **   {
 **       std::string t = NetHUD::ComposeTitle(baseTitleStr);
 **       SDL_SetWindowTitle(s_window, t.c_str());
 **   }
 **/

#ifndef INCLUDED_NETHUD_H
#define INCLUDED_NETHUD_H

#include "NetMetrics.h"
#include <string>
#include <cstdio>
#include <chrono>

namespace NetHUD
{
    // -----------------------------------------------------------------
    //  Throttle: retorna true no maximo 1x a cada 'intervalSec' segundos.
    //  Assim o titulo atualiza suave (default: 4x por segundo).
    // -----------------------------------------------------------------
    inline bool ShouldUpdate(double intervalSec = 0.25)
    {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> diff = now - last;
        if (diff.count() >= intervalSec)
        {
            last = now;
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------
    //  Monta a string completa do titulo.
    //  base    : titulo base ja pronto (ex.: "Supermodel - Sega Rally 2")
    //  showRb  : se false, omite o campo de rollback (util enquanto o
    //            rollback ainda nao foi implementado). Default: true.
    // -----------------------------------------------------------------
    inline std::string ComposeTitle(const char* base, bool showRb = true)
    {
        const NetMetrics& m = NetMetrics::Get();
        char buf[256];

        if (!m.IsConnected())
        {
            // Rede ligada mas ainda sem par conectado.
            snprintf(buf, sizeof(buf), "%s | NET: aguardando conexao", base);
            return std::string(buf);
        }

        if (showRb)
        {
            snprintf(buf, sizeof(buf),
                     "%s | PING: %.0fms | IN: %df | RB: %df",
                     base,
                     m.GetPingMs(),
                     m.GetInputDelayFrames(),
                     m.GetRollbackFrames());
        }
        else
        {
            snprintf(buf, sizeof(buf),
                     "%s | PING: %.0fms | IN: %df",
                     base,
                     m.GetPingMs(),
                     m.GetInputDelayFrames());
        }

        return std::string(buf);
    }

    // -----------------------------------------------------------------
    //  Versao "verbosa" com pico de ping e rollback maximo, util pra
    //  debugar jitter durante os testes. Troque na chamada se quiser.
    // -----------------------------------------------------------------
    inline std::string ComposeTitleVerbose(const char* base)
    {
        const NetMetrics& m = NetMetrics::Get();
        char buf[320];

        if (!m.IsConnected())
        {
            snprintf(buf, sizeof(buf), "%s | NET: aguardando conexao", base);
            return std::string(buf);
        }

        snprintf(buf, sizeof(buf),
                 "%s | PING: %.0fms (peak %.0f) | IN: %df | RB: %df (max %d)",
                 base,
                 m.GetPingMs(),
                 m.GetPingPeakMs(),
                 m.GetInputDelayFrames(),
                 m.GetRollbackFrames(),
                 m.GetMaxRollbackFrames());

        return std::string(buf);
    }
}

#endif // INCLUDED_NETHUD_H
