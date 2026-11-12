#pragma once

#include <cstdint>

/* The GCM command path with no GPU, driven by a real FIFO reader.
 *
 * The title's libgcm writes NV47 method packets into its command ring in main
 * memory and kicks the RSX by storing the ring's io offset into the control
 * register's put pointer. On hardware the command processor consumes the ring,
 * executes the methods and writes the back-end labels. In this build there is no
 * command processor, so label[255] was never written and the title spun:
 *
 *     L_2da590:  cellGcmGetLabelAddress(255) -> label
 *                if label == counter goto done; usleep(30); goto L_2da590
 *
 * This module IS the command processor, minus drawing: it walks the ring from get
 * to put, decodes the NV47 packets, executes the methods that carry synchronisation
 * (406E/4097 semaphores, SET_REFERENCE, reports, flips) and COUNTS the drawing
 * methods without executing them.
 *
 * The protocol pieces are RPCS3's, unchanged:
 *   refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv4097.cpp:784  texture read release (raw value)
 *   refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv4097.cpp:817  back-end release (value byte-swapped)
 *   refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv406e.cpp:113  NV406E release
 *   refs/rpcs3/rpcs3/Emu/RSX/NV47/HW/nv47_sync.hpp:14 write_gcm_label writes FOUR bytes
 * and the address decode is rsx_utils.cpp:224 (get_address).
 */
namespace gpu {
namespace gcm {

/* Records the ring the title handed to _cellGcmInitBody. Idempotent: a second
 * call with the same arguments changes nothing. */
void init(uint32_t context_ea, uint32_t fifo_ea, uint32_t fifo_size);
bool inited();

/* The CellGcmControl block the title addresses through cellGcmGetControlRegister:
 * +0 put, +4 get, +8 ref, all io offsets, all big-endian. */
uint32_t control_address();

/* The label window: cellGcmGetLabelAddress(index) is base + 0x10 * index. */
uint32_t label_address();
uint32_t label_address(uint32_t index);

/* cellGcmAddressToOffset: an EA the ring reader can address -> io offset. */
int32_t ea_to_io(uint32_t ea, uint32_t* io);
uint32_t io_to_ea(uint32_t io);

/* Consume everything between get and put. Called from the HLE entry points the
 * title reaches after it kicks the ring, so the whole path stays cooperative and
 * needs no thread in the browser. */
void drain();

/* Measurements, read once at exit. */
struct Stats {
    uint64_t drains = 0;
    uint64_t methods = 0;
    uint64_t draws = 0;
    uint64_t clears = 0;
    uint64_t releases = 0;
    uint64_t references = 0;
    uint64_t flips = 0;
    uint64_t packets = 0;
    uint32_t label255 = 0;
};
void stats(Stats* out);

/* Top method numbers seen, printed once: does the flip command appear at all? */
void dump_method_hist();

}  // namespace gcm
}  // namespace gpu
