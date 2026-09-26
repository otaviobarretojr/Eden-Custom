// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

namespace Common {
class DynamicLibrary;
}

namespace Vulkan {

class StreamlineBootstrap final {
public:
    StreamlineBootstrap();
    ~StreamlineBootstrap();

    StreamlineBootstrap(const StreamlineBootstrap&) = delete;
    StreamlineBootstrap& operator=(const StreamlineBootstrap&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

private:
    std::unique_ptr<Common::DynamicLibrary> interposer;
    bool initialized{};
};

[[nodiscard]] bool IsStreamlineBootstrapReady() noexcept;

} // namespace Vulkan
