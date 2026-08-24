# Integração do Rollback — Passo a passo com nomes reais

> Baseado na leitura de Model3.h, Model3.cpp, IEmulator.h e BlockFile.h.
> Este documento lista as edições concretas. O único pré-requisito que falta
> é habilitar snapshots em MEMÓRIA no CBlockFile (precisa de BlockFile.cpp).

---

## 0. Diagnóstico dos arquivos

| Arquivo | Achado relevante |
|---------|------------------|
| `Model3.cpp` | `SaveState(CBlockFile*)` serializa RAM(8MB)+backup+security+todos os devices. `RunFrame()` single-thread faz PPC→SyncGPUs→Render→Sound→Net em sequência. |
| `IEmulator.h` | Interface abstrata; SaveState/LoadState/RunFrame são virtuais puros. |
| `BlockFile.h` | **Só disco** (`FILE* fp`, `Create(file)`, `Load(file)`). Sem modo memória. |
| `Model3.h` | Estado completo em membros; `INetBoard* NetBoard` acessível via `GetNetBoard()`. |

**Bloqueador:** snapshot precisa ir para RAM, mas CBlockFile só grava em disco.

---

## 1. Pré-requisito: CBlockFile com backing em memória

### Abordagem (Linux/Steam Deck): open_memstream / fmemopen
Adicionar dois métodos ao CBlockFile que criam um `FILE*` respaldado por memória
em vez de arquivo. Todo o resto (fwrite/fread já existentes) permanece igual.

**BlockFile.h — novas declarações (public):**
```cpp
// Cria um bloco em memória para escrita. Ao Close(), 'outBuf' recebe os bytes.
Result CreateInMemory(uint8_t** outBuf, size_t* outSize,
                      const std::string& headerName, const std::string& comment);

// Abre um bloco a partir de um buffer de memória para leitura.
Result LoadFromMemory(const uint8_t* buf, size_t size);
```

**BlockFile.cpp — implementação (esqueleto):**
```cpp
#include <cstdio>

Result CBlockFile::CreateInMemory(uint8_t** outBuf, size_t* outSize,
    const std::string& headerName, const std::string& comment)
{
#if defined(_WIN32)
    return Result::FAIL; // Windows: implementar depois (ver secao 5)
#else
    fp = open_memstream((char**)outBuf, outSize); // FILE* respaldado em RAM
    if (!fp) return Result::FAIL;
    mode = 'w';
    fileSize = 0;
    WriteBlockHeader(headerName, comment); // reaproveita o codigo existente
    return Result::OKAY;
#endif
}

Result CBlockFile::LoadFromMemory(const uint8_t* buf, size_t size)
{
#if defined(_WIN32)
    return Result::FAIL;
#else
    fp = fmemopen((void*)buf, size, "rb");
    if (!fp) return Result::FAIL;
    mode = 'r';
    fileSize = (long)size;
    // (replicar aqui a validacao de header que Load(file) faz)
    return Result::OKAY;
#endif
}
```
> IMPORTANTE: preciso ver BlockFile.cpp para replicar exatamente a validação
> de header (`Load`) e a finalização (`Close`/`UpdateBlockSize`) sem quebrar o
> formato. As linhas acima são o conceito, não o código final.

---

## 2. CModel3 — save/load em memória

**Model3.h (public, junto de SaveState/LoadState):**
```cpp
void SaveStateToMemory(std::vector<uint8_t>& out);
void LoadStateFromMemory(const std::vector<uint8_t>& in);
```

**Model3.cpp:**
```cpp
void CModel3::SaveStateToMemory(std::vector<uint8_t>& out)
{
    CBlockFile bf;
    uint8_t* buf = nullptr;
    size_t   sz  = 0;
    if (Result::OKAY != bf.CreateInMemory(&buf, &sz,
            "Supermodel Save State", "rollback snapshot"))
        return;
    SaveState(&bf);   // reusa TODA a serializacao existente
    bf.Close();       // open_memstream finaliza buf/sz aqui
    if (buf && sz) { out.assign(buf, buf + sz); free(buf); }
}

void CModel3::LoadStateFromMemory(const std::vector<uint8_t>& in)
{
    if (in.empty()) return;
    CBlockFile bf;
    if (Result::OKAY != bf.LoadFromMemory(in.data(), in.size()))
        return;
    LoadState(&bf);   // reusa TODA a desserializacao existente
    bf.Close();
}
```

---

## 3. CModel3 — passo "headless" (avançar sem render/áudio)

Rollback re-simula N frames dentro de 1 frame de orçamento. NÃO queremos
renderizar nem gerar áudio nesses passos (desperdício + glitch de som).

**Model3.h (public):**
```cpp
void RunFrameHeadless(void);   // avanca 1 frame de logica, sem render/som
```

**Model3.cpp** (baseado no RunFrame single-thread atual):
```cpp
void CModel3::RunFrameHeadless(void)
{
    // Somente logica deterministica; sem RenderFrame(), sem RunSoundBoardFrame()
    RunMainBoardFrame();       // PPC
    SyncGPUs();                // mantem GPUs coerentes para o proximo estado
    if (NetBoard->IsRunning())
        RunNetBoardFrame();
    // NOTA: TileGen/GPU podem exigir um "flush" leve; validar empiricamente.
}
```

---

## 4. Orquestração no Main.cpp + detecção no SimNetBoard

### 4.1 SimNetBoard: expor divergência
No `State::ready`, quando um input CONFIRMADO chega e difere da predição usada
no frame `targetFrame`, marcar:
```cpp
m_rollbackNeeded    = true;
m_firstWrongFrame   = targetFrame;
```
E adicionar:
```cpp
bool NeedsRollback(uint32_t* outFrame) {
    if (!m_rollbackNeeded) return false;
    *outFrame = m_firstWrongFrame;
    m_rollbackNeeded = false;
    return true;
}
```

### 4.2 Main.cpp: ligar o RollbackManager
```cpp
#include "Network/RollbackManager.h"
static RollbackManager s_rollback;

// ...apos criar o Model3, antes do loop:
s_rollback.Configure(8);
s_rollback.SetCallbacks(
    [&](std::vector<uint8_t>& b){ static_cast<CModel3*>(Model3)->SaveStateToMemory(b); },
    [&](const std::vector<uint8_t>& b){ static_cast<CModel3*>(Model3)->LoadStateFromMemory(b); },
    [&](uint32_t /*frame*/){ static_cast<CModel3*>(Model3)->RunFrameHeadless(); });

// ...no loop, apos Model3->RunFrame():
static uint32_t s_frame = 0;
++s_frame;
s_rollback.CaptureSnapshot(s_frame);

uint32_t wrong;
if (auto* net = static_cast<CModel3*>(Model3)->GetNetBoard()) {
    // (via metodo novo NeedsRollback exposto no SimNetBoard)
}
```

---

## 5. Riscos e itens a validar (honestidade)

1. **Custo de memória:** ~8MB+ por snapshot × anel. Comece com anel de 4-6.
2. **Custo de CPU:** re-simular N frames + save/load pode estourar 16ms. Medir.
3. **GPU/TileGen no headless:** SyncGPUs pode não bastar; validar que o estado
   pós-rollback renderiza correto no frame seguinte.
4. **Determinismo:** o core PPC é global/não-reentrante (ok p/ 1 instancia),
   mas qualquer timing não-determinístico quebra o rollback. Testar.
5. **Windows:** open_memstream/fmemopen são POSIX. Para Windows depois, usar
   um FILE* de memória alternativo (ex.: tmpfile em RAM, ou buffer manual).
6. **Áudio:** primeira versão com som "mudo" durante a re-simulação.

---

## 6. Arquivo que ainda preciso

- `Src/Util/BlockFile.cpp` — para implementar CreateInMemory/LoadFromMemory
  replicando EXATAMENTE o formato de header/close atual, sem corromper o
  save state. É a última peça que falta para o rollback funcionar de ponta a
  ponta.
