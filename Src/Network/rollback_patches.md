# Patches do Rollback — código pronto para colar

Todas as adições são **aditivas** (nada é removido). Alvo: Linux/Steam Deck
(POSIX). Há fallback para Windows com `tmpfile()` (funcional; otimizamos depois).

---

## 1) BlockFile.h — declarações novas

Localize (bloco `public:`), logo após a declaração de `Load`:
```cpp
	Result Load(const std::string &file);
```
Adicione DEPOIS dela:
```cpp
	// --- Snapshots em memória (rollback) ---
	// Cria um bloco em memória para escrita. Ao Close(), o buffer é finalizado.
	// Os ponteiros outBuf/outSize DEVEM permanecer válidos até chamar Close().
	Result CreateInMemory(uint8_t** outBuf, size_t* outSize,
	                      const std::string& headerName,
	                      const std::string& comment);

	// Abre um bloco a partir de um buffer de memória para leitura.
	Result LoadFromMemory(const uint8_t* buf, size_t size);
```

Ainda no BlockFile.h, garanta os includes no topo (se já houver `<cstdint>`,
só falta `<cstddef>` para `size_t`):
```cpp
#include <cstdint>
#include <cstddef>
```

---

## 2) BlockFile.cpp — implementações novas

No topo, junto dos outros includes, adicione:
```cpp
#include <cstdlib>   // malloc/free (open_memstream)
```

Adicione estes dois métodos ANTES do construtor `CBlockFile::CBlockFile`:

```cpp
/******************************************************************************
 Memory-backed blocks (rollback snapshots)
******************************************************************************/

Result CBlockFile::CreateInMemory(uint8_t** outBuf, size_t* outSize,
                                  const std::string& headerName,
                                  const std::string& comment)
{
	if (outBuf == nullptr || outSize == nullptr)
		return Result::FAIL;

	*outBuf  = nullptr;
	*outSize = 0;

#if defined(_WIN32)
	// Fallback portátil: FILE* temporário (em disco no Windows).
	// Funcional para validar; substituir por buffer de memória depois.
	fp = tmpfile();
	if (NULL == fp)
		return Result::FAIL;
	mode = 'w';
	m_memWinTemp = true;          // sinaliza que precisamos ler de volta no Close
	m_memOutBuf  = outBuf;
	m_memOutSize = outSize;
	WriteBlockHeader(headerName, comment);
	return Result::OKAY;
#else
	// open_memstream dá um FILE* seekable respaldado por RAM.
	// buf/size só ficam válidos após fflush/fclose.
	fp = open_memstream(reinterpret_cast<char**>(outBuf), outSize);
	if (NULL == fp)
		return Result::FAIL;
	mode = 'w';
	WriteBlockHeader(headerName, comment);
	return Result::OKAY;
#endif
}

Result CBlockFile::LoadFromMemory(const uint8_t* buf, size_t size)
{
	if (buf == nullptr || size == 0)
		return Result::FAIL;

#if defined(_WIN32)
	// Fallback: grava em tmpfile e lê de volta.
	fp = tmpfile();
	if (NULL == fp)
		return Result::FAIL;
	fwrite(buf, 1, size, fp);
	fflush(fp);
	fseek(fp, 0, SEEK_SET);
	mode = 'r';
	fileSize = (long int)size;
	return Result::OKAY;
#else
	fp = fmemopen(const_cast<uint8_t*>(buf), size, "rb");
	if (NULL == fp)
		return Result::FAIL;
	mode = 'r';
	fileSize = (long int)size;
	return Result::OKAY;
#endif
}
```

### 2b) (Somente se você habilitar o fallback Windows) ajuste do Close()
No Linux NÃO precisa mexer no Close(). Para Windows, o Close precisa copiar o
tmpfile de volta ao buffer. Se for testar só no Deck agora, PULE este passo.
```cpp
void CBlockFile::Close(void)
{
#if defined(_WIN32)
	if (fp != nullptr && m_memWinTemp && mode == 'w' &&
	    m_memOutBuf && m_memOutSize)
	{
		fflush(fp);
		long int sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (sz > 0)
		{
			*m_memOutBuf = (uint8_t*)malloc((size_t)sz);
			if (*m_memOutBuf)
			{
				fread(*m_memOutBuf, 1, (size_t)sz, fp);
				*m_memOutSize = (size_t)sz;
			}
		}
		m_memWinTemp = false;
		m_memOutBuf = nullptr;
		m_memOutSize = nullptr;
	}
#endif
	if (fp != nullptr)
		fclose(fp);
	fp = nullptr;
	mode = 0;
}
```
E, no BlockFile.h (private), os membros do fallback Windows:
```cpp
#if defined(_WIN32)
	bool      m_memWinTemp = false;
	uint8_t** m_memOutBuf  = nullptr;
	size_t*   m_memOutSize = nullptr;
#endif
```

---

## 3) Model3.h — novos métodos públicos

Localize (dentro de `public:`, junto do bloco IEmulator):
```cpp
	void SaveState(CBlockFile *SaveState);
	void LoadState(CBlockFile *SaveState);
```
Adicione DEPOIS:
```cpp
	// --- Rollback: snapshots em memória e passo headless ---
	void SaveStateToMemory(std::vector<uint8_t>& out);
	void LoadStateFromMemory(const std::vector<uint8_t>& in);
	void RunFrameHeadless(void);   // avança 1 frame de lógica, sem render/som
```
Garanta o include no topo do Model3.h:
```cpp
#include <vector>
#include <cstdint>
```

---

## 4) Model3.cpp — implementações

Adicione logo após a implementação de `CModel3::LoadState(...)`:

```cpp
void CModel3::SaveStateToMemory(std::vector<uint8_t>& out)
{
	out.clear();

	CBlockFile bf;
	uint8_t* buf = nullptr;
	size_t   sz  = 0;

	if (Result::OKAY != bf.CreateInMemory(&buf, &sz,
	        "Supermodel Save State", "rollback snapshot"))
		return;

	SaveState(&bf);   // reusa TODA a serialização existente
	bf.Close();       // finaliza buf/sz (open_memstream)

	if (buf && sz)
	{
		out.assign(buf, buf + sz);
		free(buf);    // open_memstream aloca com malloc
	}
}

void CModel3::LoadStateFromMemory(const std::vector<uint8_t>& in)
{
	if (in.empty())
		return;

	CBlockFile bf;
	if (Result::OKAY != bf.LoadFromMemory(in.data(), in.size()))
		return;

	LoadState(&bf);   // reusa TODA a desserialização existente
	bf.Close();
}

void CModel3::RunFrameHeadless(void)
{
	// Re-simulação determinística: apenas lógica, sem RenderFrame() nem som.
	RunMainBoardFrame();       // PPC
	SyncGPUs();                // mantém snapshots de GPU/TileGen coerentes
	if (NetBoard->IsRunning())
		RunNetBoardFrame();
	// NOTA: som e render são intencionalmente omitidos.
}
```

Inclua no topo do Model3.cpp (provavelmente já há):
```cpp
#include "Util/BlockFile.h"
#include <vector>
#include <cstdlib>
```
