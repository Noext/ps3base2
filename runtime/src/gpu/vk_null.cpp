/* The 58 Vulkan entry points rsx_renderer.cpp and gpu/ring.cpp call, as NO-OPS.
 *
 * Why this file exists and what it is NOT: K0-bis measured that the RSX renderer
 * compiles for wasm32 (4650 lines, 489737 B) but does not LINK -- it calls 58 vk*
 * entry points directly, from its own code and from gpu/ring.cpp. Those calls are
 * the GPU SUBMISSION. The DECODE -- walking the guest's NV47 stream, expanding
 * vertex arrays, decoding textures, decompiling both programs, assembling the four
 * uniform blocks -- is plain CPU code with no Vulkan in it, and that is the half
 * the web port needs.
 *
 * So this is not a fake renderer and it does not claim to be one. It makes
 * submission a no-op and says so: a build linked with this file draws nothing by
 * itself. What it yields is the renderer's own STATE at every draw, in host
 * memory, which the WebGPU producer serialises into the command ring for the page
 * to replay. The page is what draws.
 *
 * Signatures are GENERATED from third_party/Vulkan-Headers/include/vulkan/
 * vulkan_core.h (the PFN_vk* typedefs), not written by hand: a hand-written stub
 * with one wrong parameter type links silently on wasm and corrupts the caller's
 * stack. Every create returns VK_SUCCESS and writes a non-null sentinel handle,
 * because a null handle makes init_objects() fail and the whole decode path is
 * skipped -- which would turn "no Vulkan" into "no measurement".
 */
#if defined(__EMSCRIPTEN__)

#include "vk_context.h"

#include <cstdint>
#include <cstring>

namespace gpu {

static uint64_t g_serial = 0;

void* fake_handle(uint32_t kind, uint64_t id) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000u + (uint64_t(kind) << 20) + id));
}

static VkCtx g_null_ctx{};

const VkCtx& vk() {
    if (!g_null_ctx.device) {
        g_null_ctx.device = reinterpret_cast<VkDevice>(fake_handle(1, 1));
        g_null_ctx.phys = reinterpret_cast<VkPhysicalDevice>(fake_handle(2, 1));
        g_null_ctx.queue = reinterpret_cast<VkQueue>(fake_handle(3, 1));
        g_null_ctx.queue_family = 0;
        /* depthBounds and occlusionQueryPrecise gate real work in the renderer, so
         * they are reported ABSENT rather than pretending a capability this path
         * cannot exercise. */
        std::memset(&g_null_ctx.features, 0, sizeof g_null_ctx.features);
        g_null_ctx.cache_control = false;
    }
    return g_null_ctx;
}

VkCommandBuffer frame_cmd() { return reinterpret_cast<VkCommandBuffer>(fake_handle(4, 1)); }
VkCommandBuffer pre_cmd() { return reinterpret_cast<VkCommandBuffer>(fake_handle(4, 2)); }
uint32_t find_memory(uint32_t, VkMemoryPropertyFlags) { return 0; }
void image_barrier(VkCommandBuffer, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout) {}
void present_image(VkImage, VkImageLayout&, uint32_t, uint32_t) {}
double last_fence_wait_ms() { return 0.0; }
double last_gpu_ms() { return 0.0; }
void submit_partial() {}
uint64_t cmd_generation() { return 0; }

}  // namespace gpu

extern "C" {

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    (void)device;
    (void)pAllocateInfo;
    (void)pAllocator;
    (void)pMemory;
    if (pMemory) *pMemory = reinterpret_cast<VkDeviceMemory>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    (void)device;
    (void)buffer;
    (void)memory;
    (void)memoryOffset;

    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    (void)device;
    (void)image;
    (void)memory;
    (void)memoryOffset;

    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) {
    (void)commandBuffer;
    (void)queryPool;
    (void)query;
    (void)flags;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    (void)commandBuffer;
    (void)pRenderingInfo;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)indexType;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)pipeline;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    (void)commandBuffer;
    (void)firstBinding;
    (void)bindingCount;
    (void)pBuffers;
    (void)pOffsets;
    (void)pSizes;
    (void)pStrides;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) {
    (void)commandBuffer;
    (void)attachmentCount;
    (void)pAttachments;
    (void)rectCount;
    (void)pRects;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pColor;
    (void)rangeCount;
    (void)pRanges;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    (void)commandBuffer;
    (void)srcBuffer;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) {
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    (void)commandBuffer;
    (void)vertexCount;
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    (void)commandBuffer;
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    (void)commandBuffer;
    (void)queryPool;
    (void)query;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdEndRendering(VkCommandBuffer commandBuffer) {
    (void)commandBuffer;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    (void)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) {
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)layout;
    (void)set;
    (void)descriptorWriteCount;
    (void)pDescriptorWrites;

    (void)0;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    (void)commandBuffer;
    (void)queryPool;
    (void)firstQuery;
    (void)queryCount;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) {
    (void)commandBuffer;
    (void)blendConstants[4];
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode) {
    (void)commandBuffer;
    (void)cullMode;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) {
    (void)commandBuffer;
    (void)depthBiasConstantFactor;
    (void)depthBiasClamp;
    (void)depthBiasSlopeFactor;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable) {
    (void)commandBuffer;
    (void)depthBiasEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    (void)commandBuffer;
    (void)minDepthBounds;
    (void)maxDepthBounds;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable) {
    (void)commandBuffer;
    (void)depthBoundsTestEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp) {
    (void)commandBuffer;
    (void)depthCompareOp;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable) {
    (void)commandBuffer;
    (void)depthTestEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable) {
    (void)commandBuffer;
    (void)depthWriteEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace) {
    (void)commandBuffer;
    (void)frontFace;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    (void)commandBuffer;
    (void)lineWidth;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable) {
    (void)commandBuffer;
    (void)primitiveRestartEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology) {
    (void)commandBuffer;
    (void)primitiveTopology;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    (void)commandBuffer;
    (void)firstScissor;
    (void)scissorCount;
    (void)pScissors;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) {
    (void)commandBuffer;
    (void)faceMask;
    (void)compareMask;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) {
    (void)commandBuffer;
    (void)faceMask;
    (void)failOp;
    (void)passOp;
    (void)depthFailOp;
    (void)compareOp;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) {
    (void)commandBuffer;
    (void)faceMask;
    (void)reference;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable) {
    (void)commandBuffer;
    (void)stencilTestEnable;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) {
    (void)commandBuffer;
    (void)faceMask;
    (void)writeMask;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    (void)commandBuffer;
    (void)firstViewport;
    (void)viewportCount;
    (void)pViewports;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pBuffer;
    if (pBuffer) *pBuffer = reinterpret_cast<VkBuffer>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSetLayout;
    if (pSetLayout) *pSetLayout = reinterpret_cast<VkDescriptorSetLayout>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    (void)device;
    (void)pipelineCache;
    (void)createInfoCount;
    (void)pCreateInfos;
    (void)pAllocator;
    (void)pPipelines;
    if (pPipelines) *pPipelines = reinterpret_cast<VkPipeline>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pImage;
    if (pImage) *pImage = reinterpret_cast<VkImage>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pView;
    if (pView) *pView = reinterpret_cast<VkImageView>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pPipelineCache;
    if (pPipelineCache) *pPipelineCache = reinterpret_cast<VkPipelineCache>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pPipelineLayout;
    if (pPipelineLayout) *pPipelineLayout = reinterpret_cast<VkPipelineLayout>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pQueryPool;
    if (pQueryPool) *pQueryPool = reinterpret_cast<VkQueryPool>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSampler;
    if (pSampler) *pSampler = reinterpret_cast<VkSampler>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pShaderModule;
    if (pShaderModule) *pShaderModule = reinterpret_cast<VkShaderModule>(gpu::fake_handle(9, ++gpu::g_serial));
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    (void)device;
    (void)image;
    (void)pAllocator;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
    (void)device;
    (void)imageView;
    (void)pAllocator;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    (void)device;
    (void)memory;
    (void)pAllocator;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
    (void)device;
    (void)buffer;
    (void)pMemoryRequirements;
}

static VKAPI_ATTR void VKAPI_CALL r3_null_vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    (void)device;
    (void)image;
    (void)pMemoryRequirements;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) {
    (void)device;
    (void)pipelineCache;
    (void)pDataSize;
    (void)pData;

    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)device;
    (void)queryPool;
    (void)firstQuery;
    (void)queryCount;
    (void)dataSize;
    (void)pData;
    (void)stride;
    (void)flags;

    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL r3_null_vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
    (void)device;
    (void)memory;
    (void)offset;
    (void)size;
    (void)flags;
    (void)ppData;
    if (ppData) *ppData = nullptr;
    return VK_SUCCESS;
}

/* volk declares every vk* name as a FUNCTION POINTER (volk.h: "extern PFN_vkXxx
 * vkXxx;"), so the null backend replaces the POINTER, not the symbol: defining
 * vkCmdDrawIndexed as a function is a redefinition of a different kind and does not
 * compile. Each pointer names its stub explicitly here, so the 58 are visible in
 * one list. */

PFN_vkAllocateMemory vkAllocateMemory = r3_null_vkAllocateMemory;
PFN_vkBindBufferMemory vkBindBufferMemory = r3_null_vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory = r3_null_vkBindImageMemory;
PFN_vkCmdBeginQuery vkCmdBeginQuery = r3_null_vkCmdBeginQuery;
PFN_vkCmdBeginRendering vkCmdBeginRendering = r3_null_vkCmdBeginRendering;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = r3_null_vkCmdBindIndexBuffer;
PFN_vkCmdBindPipeline vkCmdBindPipeline = r3_null_vkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers2 vkCmdBindVertexBuffers2 = r3_null_vkCmdBindVertexBuffers2;
PFN_vkCmdClearAttachments vkCmdClearAttachments = r3_null_vkCmdClearAttachments;
PFN_vkCmdClearColorImage vkCmdClearColorImage = r3_null_vkCmdClearColorImage;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = r3_null_vkCmdCopyBufferToImage;
PFN_vkCmdCopyImage vkCmdCopyImage = r3_null_vkCmdCopyImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = r3_null_vkCmdCopyImageToBuffer;
PFN_vkCmdDraw vkCmdDraw = r3_null_vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed = r3_null_vkCmdDrawIndexed;
PFN_vkCmdEndQuery vkCmdEndQuery = r3_null_vkCmdEndQuery;
PFN_vkCmdEndRendering vkCmdEndRendering = r3_null_vkCmdEndRendering;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = r3_null_vkCmdPipelineBarrier;
PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR = r3_null_vkCmdPushDescriptorSetKHR;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool = r3_null_vkCmdResetQueryPool;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants = r3_null_vkCmdSetBlendConstants;
PFN_vkCmdSetCullMode vkCmdSetCullMode = r3_null_vkCmdSetCullMode;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias = r3_null_vkCmdSetDepthBias;
PFN_vkCmdSetDepthBiasEnable vkCmdSetDepthBiasEnable = r3_null_vkCmdSetDepthBiasEnable;
PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds = r3_null_vkCmdSetDepthBounds;
PFN_vkCmdSetDepthBoundsTestEnable vkCmdSetDepthBoundsTestEnable = r3_null_vkCmdSetDepthBoundsTestEnable;
PFN_vkCmdSetDepthCompareOp vkCmdSetDepthCompareOp = r3_null_vkCmdSetDepthCompareOp;
PFN_vkCmdSetDepthTestEnable vkCmdSetDepthTestEnable = r3_null_vkCmdSetDepthTestEnable;
PFN_vkCmdSetDepthWriteEnable vkCmdSetDepthWriteEnable = r3_null_vkCmdSetDepthWriteEnable;
PFN_vkCmdSetFrontFace vkCmdSetFrontFace = r3_null_vkCmdSetFrontFace;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth = r3_null_vkCmdSetLineWidth;
PFN_vkCmdSetPrimitiveRestartEnable vkCmdSetPrimitiveRestartEnable = r3_null_vkCmdSetPrimitiveRestartEnable;
PFN_vkCmdSetPrimitiveTopology vkCmdSetPrimitiveTopology = r3_null_vkCmdSetPrimitiveTopology;
PFN_vkCmdSetScissor vkCmdSetScissor = r3_null_vkCmdSetScissor;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask = r3_null_vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilOp vkCmdSetStencilOp = r3_null_vkCmdSetStencilOp;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference = r3_null_vkCmdSetStencilReference;
PFN_vkCmdSetStencilTestEnable vkCmdSetStencilTestEnable = r3_null_vkCmdSetStencilTestEnable;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask = r3_null_vkCmdSetStencilWriteMask;
PFN_vkCmdSetViewport vkCmdSetViewport = r3_null_vkCmdSetViewport;
PFN_vkCreateBuffer vkCreateBuffer = r3_null_vkCreateBuffer;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = r3_null_vkCreateDescriptorSetLayout;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = r3_null_vkCreateGraphicsPipelines;
PFN_vkCreateImage vkCreateImage = r3_null_vkCreateImage;
PFN_vkCreateImageView vkCreateImageView = r3_null_vkCreateImageView;
PFN_vkCreatePipelineCache vkCreatePipelineCache = r3_null_vkCreatePipelineCache;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout = r3_null_vkCreatePipelineLayout;
PFN_vkCreateQueryPool vkCreateQueryPool = r3_null_vkCreateQueryPool;
PFN_vkCreateSampler vkCreateSampler = r3_null_vkCreateSampler;
PFN_vkCreateShaderModule vkCreateShaderModule = r3_null_vkCreateShaderModule;
PFN_vkDestroyImage vkDestroyImage = r3_null_vkDestroyImage;
PFN_vkDestroyImageView vkDestroyImageView = r3_null_vkDestroyImageView;
PFN_vkFreeMemory vkFreeMemory = r3_null_vkFreeMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = r3_null_vkGetBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = r3_null_vkGetImageMemoryRequirements;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData = r3_null_vkGetPipelineCacheData;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults = r3_null_vkGetQueryPoolResults;
PFN_vkMapMemory vkMapMemory = r3_null_vkMapMemory;

}  // extern "C"

#else
/* A native build links the real volk loader; this file must be empty there so it
 * cannot shadow a real Vulkan entry point. */
#endif  // __EMSCRIPTEN__
