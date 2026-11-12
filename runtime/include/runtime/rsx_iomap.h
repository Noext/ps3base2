/* The RSX address-space mapping, shared between the HLE module that exposes it
 * (cellGcmMapEaIoAddress, cellGcmMapMainMemory) and runtime/src/lv2/rsx.cpp that
 * owns io_table and the FIFO walker. It lives in a header so both compile against
 * one definition: the mapping and the walker MUST agree, and when they did not the
 * walker reported 'FIFO get 0x1202a180 not IO-mapped' for a whole run.
 */
#ifndef PS3BASE_RSX_IOMAP_H
#define PS3BASE_RSX_IOMAP_H

#include <cstdint>

/* refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.h:8. */
static constexpr int32_t CELL_GCM_ERROR_FAILURE = static_cast<int32_t>(0x802100ffu);

/* Map size bytes of guest memory at ea to RSX io offset io, 1 MB at a time.
 * Mirrors gcmMapEaIoAddress (cellGcmSys.cpp:1064): all three arguments must be
 * 1 MB aligned, and the only effect is writing the io_table. Returns 0 on success. */
/* Both live in namespace lv2 (runtime/src/lv2/rsx.cpp), next to io_table and the
 * FIFO walker they must agree with. Declared here so the HLE module can call them
 * without redeclaring, which is how the two once disagreed. */
namespace lv2 {
int32_t gcm_map_ea_io(uint32_t ea, uint32_t io, uint32_t size);

/* True when this 1 MB io page already maps something. */
bool rsx_io_page_taken(uint32_t io);

/* RSX label table entry, or 0 when the table is not up (refs cellGcmSys.cpp:159). */
uint32_t gcm_label_address(uint32_t index);

/* Invoke the guest flip handler registered via cellGcmSetFlipHandler. */
void rsx_flip_callback(uint32_t handler_ea);

/* Create the RSX context the LLE _cellGcmInitBody would have created: local
 * memory, the context block, and the vblank/FIFO threads. */
int32_t rsx_setup_for_hle();
}  // namespace lv2

using lv2::gcm_map_ea_io;
using lv2::rsx_io_page_taken;
using lv2::gcm_label_address;
using lv2::rsx_flip_callback;
using lv2::rsx_setup_for_hle;

#endif  /* PS3BASE_RSX_IOMAP_H */