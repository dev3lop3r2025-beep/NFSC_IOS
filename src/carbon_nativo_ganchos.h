// =============================================================================
//  Need for Speed: Carbon - Native Renderer Hooks Header
// =============================================================================

#pragma once

#include <cstdint>

namespace nfsmw::nativo {

enum class FuncionDibujo {
  kVertices,
  kIndexados,
  kVerticesUP,
  kIndexadosUP,
  kCualquiera,
};

void AnotarDibujo(FuncionDibujo funcion, uint8_t* base, uint32_t r3, uint32_t r4,
                  uint32_t r5, uint32_t r6, uint32_t r7);
void TerminarDibujo();

namespace ganchos_detalle {
struct EntradaShader;
const EntradaShader* IdentificarCreacion(uint8_t* base, uint32_t container, bool is_vertex);
void RecordarCreacion(uint32_t shader_obj, const EntradaShader* entrada, bool is_vertex);
}  // namespace ganchos_detalle

}  // namespace nfsmw::nativo
