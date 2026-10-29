#include "heal_on_save.h"
#include "cheats.h"
#include <os/logger.h>
#include <settings/config.h>
#include <atomic>
#include <chrono>

namespace
{
    std::atomic<bool>& Enabled()
    {
        static std::atomic<bool> enabled{settings::GetConfig().healOnSave};
        return enabled;
    }
}

bool debug_menu::HealOnSaveEnabled()
{
    return Enabled().load(std::memory_order_relaxed);
}

void debug_menu::SetHealOnSaveEnabled(bool enabled)
{
    Enabled().store(enabled, std::memory_order_relaxed);
    if (!settings::SaveHealOnSave(enabled))
        LOG_WARNING("debug menu: failed to persist the heal-on-save setting");
    LOG_INFO("debug menu: heal on save {}", enabled);
}

void debug_menu::NotifySaveWritten()
{
    if (!HealOnSaveEnabled()) return;
    // A save writes several times; log the first of each burst.
    static std::atomic<int64_t> last{0};
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - last.exchange(now) > 5) LOG_INFO("heal on save: save written; party HP/MP restore queued for the field");
    cheats::session.RequestSaveHeal();
}
