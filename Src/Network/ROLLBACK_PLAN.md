# Plano de Integração — Rollback Netcode (Supermodel Model 3)

> Documento de arquitetura para transformar a predição atual em rollback real,
> com contagem de frames re-simulados exibida no HUD (campo **RB**).

---

## 1. Objetivo

Transformar o netcode atual (input delay + predição) em um sistema de
**rollback**: quando um input remoto confirmado divergir da predição usada,
reverter o estado do emulador para o frame correto, aplicar o input real e
**re-simular** os frames intermediários — tudo dentro do orçamento de 1 frame.

---

## 2. Por que no nível do Model 3 (e não no SimNetBoard)

- `SimNetBoard::RunFrame()` é um subcomponente chamado *dentro* da emulação e
  só administra a CommRAM da placa de rede.
- Rollback precisa reverter **o jogo inteiro** (PowerPC + Real3D + tilegen +
  som + RAM). Isso é responsabilidade do `CModel3`, dirigido pelo loop de
  `Main.cpp` (`Model3->RunFrame()`).
- O código já sinaliza isso: os stubs de `SaveState/LoadState` do SimNetBoard
  dizem *"Rollback snapshots will be integrated at the Model 3 level"*.

**Divisão de responsabilidades:**
| Camada | Papel no rollback |
|--------|-------------------|
| `SimNetBoard` | **Detecta** divergência (input previsto ≠ confirmado) e informa o frame divergente |
| `RollbackManager` | **Mecanismo**: guarda snapshots, restaura e re-simula |
| `CModel3` | **Executa**: expõe save/load em memória e "avançar 1 frame" |
| `Main.cpp` | **Orquestra**: captura snapshot por frame e dispara o Reconcile |

---

## 3. Pré-requisitos técnicos (obrigatórios)

1. **Snapshot em memória (rápido).** O `SaveState` atual grava em disco via
   `CBlockFile`. Precisamos de `SaveStateToMemory(std::vector<uint8_t>&)` e
   `LoadStateFromMemory(const std::vector<uint8_t>&)` no `CModel3`.
2. **`-no-threads`.** Determinismo frame a frame exige execução single-thread
   (o netboard já exige isso).
3. **Determinismo.** Mesmo input + mesmo estado => mesmo resultado. Qualquer
   fonte não-determinística (timing, RNG externo) quebra o rollback.
4. **Áudio.** Silenciar/descartar a saída de som durante a re-simulação para
   evitar glitches (re-simular = re-gerar samples já tocados).

---

## 4. Arquitetura proposta

```
                 ┌──────────────────────────────────────────┐
                 │              Main.cpp (loop)              │
                 │                                           │
   a cada frame  │  Model3->RunFrame();                      │
                 │  rb.CaptureSnapshot(frame);               │
                 │                                           │
   ao divergir   │  if (net->NeedsRollback(&wrongFrame))     │
                 │      rb.Reconcile(wrongFrame, frame);     │
                 └───────────────┬───────────────────────────┘
                                 │ callbacks
             ┌───────────────────┼────────────────────┐
             ▼                   ▼                    ▼
        SaveFn(blob)        LoadFn(blob)         StepFn(frame)
   Model3->SaveStateToMemory   Model3->LoadStateFromMemory   Model3->RunFrame
```

- **RollbackManager** (já entregue): anel de snapshots + Reconcile + reporta RB.
- **SimNetBoard**: novo método que expõe se houve divergência e em qual frame.
- **CModel3**: 2 métodos novos (save/load memória) — o único ponto que exige
  mexer no core do emulador.

---

## 5. O que já está pronto (entregue)

- `NetMetrics.h` — telemetria thread-safe (ping/IN/RB).
- `NetHUD.h` — HUD na barra de título.
- `RollbackManager.h` — motor de rollback por callbacks, já reportando RB.

O `RollbackManager` já compila e é independente. Falta apenas **ligá-lo** aos
métodos reais do Model 3 e à detecção de divergência do SimNetBoard.

---

## 6. Pontos de integração (o que falta ligar)

### 6.1 CModel3 — save/load em memória (o ponto crítico)
Adicionar ao `CModel3` (e à interface `IEmulator` se necessário):
```cpp
void SaveStateToMemory(std::vector<uint8_t>& out);
void LoadStateFromMemory(const std::vector<uint8_t>& in);
```
Provável caminho: adaptar o `CBlockFile` para escrever/ler de um buffer de
memória em vez de arquivo (memory-backed stream), reaproveitando o
`Model3->SaveState()/LoadState()` que já existe.

### 6.2 SimNetBoard — sinalizar divergência
Quando `GetRemoteInputForFrame` retornar um input CONFIRMADO que difere do que
foi usado como predição no mesmo frame, registrar:
```cpp
m_rollbackNeeded = true;
m_firstWrongFrame = targetFrame;
```
E expor:
```cpp
bool NeedsRollback(uint32_t* outFirstWrongFrame);
```

### 6.3 Main.cpp — orquestração
```cpp
rb.SetCallbacks(saveMem, loadMem, stepOneFrame);
rb.Configure(10);
// ...loop:
Model3->RunFrame();
rb.CaptureSnapshot(frame);
uint32_t wrong;
if (net->NeedsRollback(&wrong))
    rb.Reconcile(wrong, frame);
```

---

## 7. Riscos e limitações (honestidade)

- **Custo de memória:** cada snapshot pode ter vários MB; 10 frames de anel =
  dezenas de MB. Ajustável via `Configure()`.
- **Custo de CPU:** re-simular N frames num único frame de orçamento pode
  estourar 16ms se N for grande ou o save/load for lento. Começar com anel
  curto e rollback máximo baixo (ex.: 4-6 frames).
- **Compilar ≠ jogar liso.** Determinismo do core do Supermodel precisa ser
  validado empiricamente; pode haver ajustes finos.
- **Som:** primeira versão provavelmente com áudio "mudo" durante re-simulação.

---

## 8. Arquivos que preciso para ligar tudo

Para escrever o save/load em memória e a detecção de divergência sem chutar
nomes, preciso ver:

1. `Src/Model3/IEmulator.h` — a interface (assinaturas de SaveState/RunFrame).
2. `Src/Model3/Model3.h` e `Src/Model3/Model3.cpp` — o core concreto.
3. `Src/Util/BlockFile.h` (ou `BlockFile.cpp`) — para saber se dá para fazer
   um CBlockFile memory-backed.

Com esses 4, eu entrego as edições cirúrgicas de 6.1/6.2/6.3 e fechamos o
rollback de ponta a ponta.
