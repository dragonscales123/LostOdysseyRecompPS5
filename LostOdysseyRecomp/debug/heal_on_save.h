#pragma once

// Debug menu quick tool: refill the party's HP and MP after each save
// (settings::Config::healOnSave). A finished save write calls
// NotifySaveWritten; the next field tick heals through the cheats module's
// validated party data, so the write never happens mid-save or in battle.
namespace debug_menu
{
    bool HealOnSaveEnabled();
    void SetHealOnSaveEnabled(bool enabled);
    void NotifySaveWritten();
}
