// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <vector>

#include <boost/container/static_vector.hpp>

#include "common/common_types.h"
#include "video_core/control/channel_state_cache.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/present/dlss_inputs.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_fence_manager.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_query_cache.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {

namespace Engines {
class Maxwell3D;
}

} // namespace Tegra

namespace Vulkan {

struct FramebufferTextureInfo;

class StateTracker;

class AccelerateDMA : public Tegra::Engines::AccelerateDMAInterface {
public:
    explicit AccelerateDMA(BufferCache& buffer_cache, TextureCache& texture_cache,
                           Scheduler& scheduler);

    bool BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) override;

    bool BufferClear(GPUVAddr src_address, u64 amount, u32 value) override;

    bool ImageToBuffer(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::ImageOperand& src,
                       const Tegra::DMA::BufferOperand& dst) override;

    bool BufferToImage(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::BufferOperand& src,
                       const Tegra::DMA::ImageOperand& dst) override;

private:
    template <bool IS_IMAGE_UPLOAD>
    bool DmaBufferImageCopy(const Tegra::DMA::ImageCopy& copy_info,
                            const Tegra::DMA::BufferOperand& src,
                            const Tegra::DMA::ImageOperand& dst);

    BufferCache& buffer_cache;
    TextureCache& texture_cache;
    Scheduler& scheduler;
};

class RasterizerVulkan final : public VideoCore::RasterizerInterface,
                               protected VideoCommon::ChannelSetupCaches<VideoCommon::ChannelInfo> {
public:
    explicit RasterizerVulkan(Core::Frontend::EmuWindow& emu_window_, Tegra::GPU& gpu_,
                              Tegra::MaxwellDeviceMemoryManager& device_memory_,
                              const Device& device_, MemoryAllocator& memory_allocator_,
                              StateTracker& state_tracker_, Scheduler& scheduler_);
    ~RasterizerVulkan() override;

    void Draw(bool is_indexed, u32 instance_count) override;
    void DrawIndirect() override;
    void DrawTexture() override;
    void Clear(u32 layer_count) override;
    void DispatchCompute() override;
    void ResetCounter(VideoCommon::QueryType type) override;
    void Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
               VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) override;
    void BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr, u32 size) override;
    void DisableGraphicsUniformBuffer(size_t stage, u32 index) override;
    void FlushAll() override;
    void FlushRegion(DAddr addr, u64 size,
                     VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    bool MustFlushRegion(DAddr addr, u64 size,
                         VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override;
    void InvalidateRegion(DAddr addr, u64 size,
                          VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void InnerInvalidation(std::span<const std::pair<DAddr, std::size_t>> sequences) override;
    void OnCacheInvalidation(DAddr addr, u64 size) override;
    bool OnCPUWrite(DAddr addr, u64 size) override;
    void InvalidateGPUCache() override;
    void UnmapMemory(DAddr addr, u64 size) override;
    void ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) override;
    void SignalFence(std::function<void()>&& func) override;
    void SyncOperation(std::function<void()>&& func) override;
    void SignalSyncPoint(u32 value) override;
    void SignalReference() override;
    void ReleaseFences(bool force = true) override;
    void FlushAndInvalidateRegion(
        DAddr addr, u64 size, VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void WaitForIdle() override;
    void FragmentBarrier() override;
    void TiledCacheBarrier() override;
    void FlushCommands() override;
    void TickFrame() override;
    bool AccelerateConditionalRendering() override;
    bool HasDrawTransformFeedback() override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& src,
                               const Tegra::Engines::Fermi2D::Surface& dst,
                               const Tegra::Engines::Fermi2D::Config& copy_config) override;
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override;
    void AccelerateInlineToMemory(GPUVAddr address, size_t copy_size,
                                  std::span<const u8> memory) override;
    void LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;

    void InitializeChannel(Tegra::Control::ChannelState& channel) override;

    void BindChannel(Tegra::Control::ChannelState& channel) override;

    void ReleaseChannel(s32 channel_id) override;
    std::optional<FramebufferTextureInfo> AccelerateDisplay(const Tegra::FramebufferConfig& config,
                                                            VAddr framebuffer_addr,
                                                            u32 pixel_stride);

    struct DlssDepthCandidate {
        VkImage image{};
        VkImageView view{};
        VkExtent2D extent{};
        VkImageSubresourceRange range{};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkFormat format{VK_FORMAT_UNDEFINED};

        [[nodiscard]] bool IsValid() const {
            return image != VK_NULL_HANDLE && extent.width != 0 && extent.height != 0;
        }
    };

    [[nodiscard]] DlssDepthCandidate GetDlssDepthCandidate() const;

    struct DlssColorCandidate {
        VkImage image{};
        VkImageView view{};
        VkExtent2D extent{};
        VkImageSubresourceRange range{};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkFormat format{VK_FORMAT_UNDEFINED};
        u32 slot{};

        [[nodiscard]] bool IsValid() const {
            return image != VK_NULL_HANDLE && extent.width != 0 && extent.height != 0;
        }
    };

    [[nodiscard]] std::vector<DlssColorCandidate> GetDlssColorCandidates() const;

    struct DlssFramebufferSnapshot {
        DlssDepthCandidate depth{};
        std::vector<DlssColorCandidate> colors{};
    };

    [[nodiscard]] DlssFramebufferSnapshot GetDlssFramebufferSnapshot() const;

    struct DlssMotionSemanticSignature {
        u64 title_id{};
        u64 fragment_shader_hash{};
        u32 slot{};
        VkFormat format{VK_FORMAT_UNDEFINED};

        [[nodiscard]] bool IsComplete() const noexcept {
            return title_id != 0 && fragment_shader_hash != 0 && format != VK_FORMAT_UNDEFINED;
        }
    };

    [[nodiscard]] static bool IsVerifiedDlssMotionSignature(
        const DlssMotionSemanticSignature& signature) noexcept;

    struct DlssMotionEvidence {
        bool format_compatible{};
        bool render_resolution_compatible{};
        bool resource_persistent{};
        bool fragment_shader_writes_slot{};
        bool producer_stable{};
        bool semantic_evidence{};

        [[nodiscard]] bool IsHeuristicCandidate() const noexcept {
            return format_compatible && render_resolution_compatible && resource_persistent &&
                   fragment_shader_writes_slot && producer_stable;
        }

        [[nodiscard]] bool IsVerified() const noexcept {
            return IsHeuristicCandidate() && semantic_evidence;
        }
    };

    struct DlssMotionCandidateHistory {
        VkImage image{};
        VkImageView view{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        u32 slot{};
        u32 consecutive_frames{};
        u64 last_frame{};
        bool motion_format_compatible{};
        bool render_resolution_compatible{};
        bool persistent{};
        bool fragment_shader_writes_slot{};
        u64 fragment_shader_hash{};
        u32 producer_consecutive_frames{};
        bool producer_stable{};
        DlssMotionSemanticSignature semantic_signature{};
        bool semantic_evidence{};
        DlssMotionEvidence evidence{};
        DlssMotionConfidence confidence{DlssMotionConfidence::None};
    };

    struct DlssUniformBindingObservation {
        size_t stage{};
        u32 index{};
        GPUVAddr gpu_addr{};
        u32 size{};
        u64 title_id{};
        u64 bind_sequence{};
        u64 frame_index{};
        u64 fragment_shader_hash{};
        bool fragment_producer_unambiguous{};
        u32 consecutive_frames{};
        bool stable_binding{};
        bool plausible_temporal_size{};
        bool temporal_diagnostic_candidate{};
        u64 last_sampled_frame{};
        u64 sample_fingerprint{};
        bool sampled{};
        u64 previous_sample_fingerprint{};
        u32 sample_count{};
        u32 fingerprint_change_count{};
        bool temporally_dynamic{};
        bool strong_temporal_candidate{};
        u32 sampled_float_count{};
        u32 finite_float_count{};
        u32 normalized_float_count{};
        bool matrix_shape_candidate{};
        bool semantic_probe_candidate{};
    };

    void TrackDlssFragmentOutputs(const GraphicsPipeline& pipeline);
    void SetDlssSemanticTitleId(u64 title_id) noexcept {
        dlss_semantic_title_id = title_id;
    }
    void TrackDlssTemporalCandidates(u64 frame_index);
    [[nodiscard]] std::vector<DlssMotionCandidateHistory> GetDlssMotionCandidateHistory() const;
    [[nodiscard]] std::vector<DlssMotionCandidateHistory> GetDlssLikelyMotionCandidates() const;
    [[nodiscard]] DlssTemporalSnapshot CaptureDlssTemporalSnapshot(u64 frame_index) const;

private:
    static constexpr const u64 NEEDS_D24[] = {
        0x01006A800016E000ULL, // SSBU
        0x0100E95004038000ULL, // XC2
        0x0100A6301214E000ULL, // FE:Engage
    };
    static constexpr size_t MAX_TEXTURES = 192;
    static constexpr size_t MAX_IMAGES = 48;
    static constexpr size_t MAX_IMAGE_VIEWS = MAX_TEXTURES + MAX_IMAGES;

    static constexpr VkDeviceSize DEFAULT_BUFFER_SIZE = 4 * sizeof(float);

    template <typename Func>
    void PrepareDraw(bool is_indexed, Func&&);

    void FlushWork();

    void UpdateDynamicStates();

    void HandleTransformFeedback();

    void UpdateViewportsState(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateScissorsState(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBias(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateBlendConstants(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBounds(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilFaces(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLineWidth(Tegra::Engines::Maxwell3D::Regs& regs);

    void UpdateCullMode(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBoundsTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthCompareOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdatePrimitiveRestartEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateRasterizerDiscardEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateConservativeRasterizationMode(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLineStippleEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLineStipple(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLineRasterizationMode(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBiasEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLogicOpEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthClampEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateAlphaToCoverageEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateAlphaToOneEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateFrontFace(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLogicOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateBlending(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateColorWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs);

    void UpdateVertexInput(Tegra::Engines::Maxwell3D::Regs& regs);

    Tegra::GPU& gpu;
    Tegra::MaxwellDeviceMemoryManager& device_memory;

    const Device& device;
    MemoryAllocator& memory_allocator;
    StateTracker& state_tracker;
    Scheduler& scheduler;

    StagingBufferPool staging_pool;
    DescriptorPool descriptor_pool;
    GuestDescriptorQueue guest_descriptor_queue;
    ComputePassDescriptorQueue compute_pass_descriptor_queue;
    DescriptorBufferRing descriptor_buffer_ring;
    BlitImageHelper blit_image;
    RenderPassCache render_pass_cache;

    TextureCacheRuntime texture_cache_runtime;
    TextureCache texture_cache;
    BufferCacheRuntime buffer_cache_runtime;
    BufferCache buffer_cache;
    QueryCacheRuntime query_cache_runtime;
    QueryCache query_cache;
    PipelineCache pipeline_cache;
    AccelerateDMA accelerate_dma;
    FenceManager fence_manager;

    vk::Event wfi_event;

    boost::container::static_vector<u32, MAX_IMAGE_VIEWS> image_view_indices;
    std::array<VideoCommon::ImageViewId, MAX_IMAGE_VIEWS> image_view_ids;
    boost::container::static_vector<VkSampler, MAX_TEXTURES> sampler_handles;

    mutable std::mutex dlss_candidate_mutex;
    std::vector<DlssMotionCandidateHistory> dlss_motion_history;
    std::array<bool, 8> dlss_fragment_output_slots{};
    std::array<u64, 8> dlss_fragment_output_hashes{};
    std::array<bool, 8> dlss_fragment_output_ambiguous{};
    u64 dlss_semantic_title_id{};
    u64 dlss_uniform_bind_sequence{};
    u64 dlss_temporal_frame_index{};
    std::array<DlssUniformBindingObservation, 64> dlss_uniform_observations{};
    size_t dlss_uniform_observation_cursor{};
    u32 draw_counter = 0;
};

} // namespace Vulkan
