// What the native renderer of NFSMW Android Evolved asks of Most Wanted's own modules, which are not built for
// NFS Carbon: road reflection on demand, the 30 FPS guard, the scenery LOD summary, the D3D composite marker
// (Carbon inlines FlushState) and the AA table. Each answer is the one that leaves the renderer's own behaviour.
#include <atomic>
#include <cstdint>
#include <string>

#include "carbon_nativo_ganchos.h"

// Game frames, one per Swap; only for the "game ahead" measurement. Carbon's Swap is not hooked yet.
std::atomic<uint64_t> g_nfsmw_fotogramas_juego{0};

namespace nfsmw::reflejo_demanda {
void AnotarLectura() {}
void AnotarCopia() {}
void AnotarSwap() {}
bool MedirVisibilidad() { return false; }
bool VisibilidadComprobada() { return false; }
void AnotarVisible(bool) {}
void AnotarOculto() {}
void AnotarTestigo(bool) {}
}  // namespace nfsmw::reflejo_demanda

namespace nfsmw::guardia30 {
void Latir(double) {}
void Informe() {}
bool SinSombras(bool del_usuario) { return del_usuario; }
}  // namespace nfsmw::guardia30

namespace nfsmw::escenario_lod {
std::string Resumen() { return "LOD del escenario: no aplica en Carbon"; }
}  // namespace nfsmw::escenario_lod

namespace nfsmw::render_targets {
// Occlusion queries are counted with one sample per pixel: carbon_single_pass selects the game's mode 2 (one
// tile, no MSAA).
uint32_t MuestrasOriginalesModoActual(const uint8_t*) { return 1; }
}  // namespace nfsmw::render_targets

namespace nfsmw::nativo {
void ActivarConsumidorMarcadores(bool) {}
void AnotarComprobacionMarcador(bool, uint32_t, uint32_t, uint32_t, uint32_t) {}
void AnotarDibujo(FuncionDibujo, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void TerminarDibujo() {}

namespace ganchos_detalle {
struct EntradaShader {};
const EntradaShader* IdentificarCreacion(uint8_t*, uint32_t, bool) { return nullptr; }
void RecordarCreacion(uint32_t, const EntradaShader*, bool) {}
}  // namespace ganchos_detalle
}  // namespace nfsmw::nativo

extern "C" {
float nfsmw_mirror_scale_x = 1.0f;
float nfsmw_mirror_scale_y = 1.0f;
float nfsmw_mirror_shift_y = 0.0f;
}

struct PPCContext;

#if defined(_MSC_VER) && !defined(__clang__)
#define REX_WEAK_HOOK_FALLBACK(name) \
  extern "C" void __imp__##name(PPCContext&, uint8_t*) {}
#else
#define REX_WEAK_HOOK_FALLBACK(name) \
  __attribute__((weak)) extern "C" void __imp__##name(PPCContext&, uint8_t*) {}
#endif

REX_WEAK_HOOK_FALLBACK(sub_823C8290)
REX_WEAK_HOOK_FALLBACK(sub_824FFBE0)
REX_WEAK_HOOK_FALLBACK(sub_82500298)
REX_WEAK_HOOK_FALLBACK(sub_82592340)
REX_WEAK_HOOK_FALLBACK(sub_825930E0)
REX_WEAK_HOOK_FALLBACK(sub_82593408)
REX_WEAK_HOOK_FALLBACK(sub_82593818)
REX_WEAK_HOOK_FALLBACK(sub_82593A58)
REX_WEAK_HOOK_FALLBACK(sub_82597070)
REX_WEAK_HOOK_FALLBACK(sub_825987E0)
REX_WEAK_HOOK_FALLBACK(sub_8259A308)
REX_WEAK_HOOK_FALLBACK(sub_826DCB30)
REX_WEAK_HOOK_FALLBACK(sub_826DCD68)
REX_WEAK_HOOK_FALLBACK(sub_826DD448)
REX_WEAK_HOOK_FALLBACK(sub_826DD9D8)
REX_WEAK_HOOK_FALLBACK(sub_826DDA78)
REX_WEAK_HOOK_FALLBACK(sub_826DDE68)
REX_WEAK_HOOK_FALLBACK(sub_826DE280)
REX_WEAK_HOOK_FALLBACK(sub_826DE700)
REX_WEAK_HOOK_FALLBACK(sub_826E0250)
REX_WEAK_HOOK_FALLBACK(sub_826E4680)
REX_WEAK_HOOK_FALLBACK(sub_826F6A88)
REX_WEAK_HOOK_FALLBACK(sub_82706068)
