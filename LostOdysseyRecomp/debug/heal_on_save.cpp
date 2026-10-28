#include "heal_on_save.h"
#include "cheats.h"
#include <os/logger.h>
#include <settings/config.h>
#include <atomic>

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
    if (HealOnSaveEnabled())
        cheats::session.RequestSaveHeal();
}
