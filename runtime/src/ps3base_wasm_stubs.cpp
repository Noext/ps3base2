/* One symbol the generic ps3base runtime needs and no God of War module provides.
 *
 * spu/thread.cpp reads edge::gpu::g_active before routing a DMA transfer through
 * GOWA's edge accelerator. spu/edge/edge_gpu.cpp defines it, but that file also
 * pulls the whole edge GPU state (g_mode, the descriptor tables) and belongs to a
 * title we are not running, so it stays out of this build.
 *
 * The flag lives here instead, alone, in its own translation unit: earlier
 * attempts to add it to gowa_stubs.cpp collided with a parallel port that adds and
 * removes its own copy there, and the definitions fought each other.
 *
 * false means the transfer takes the ordinary path, which is what this title
 * wants -- it has no edge GPU to accelerate anything with.
 */
namespace edge {
namespace gpu {
bool g_active = false;
}  // namespace gpu
}  // namespace edge
