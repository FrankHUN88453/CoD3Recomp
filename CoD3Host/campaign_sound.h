#pragma once

#include <string>
#include <string_view>
#include <cctype>

// A copy's events, as this game names them: weapon_<copy>_weapon_<what>
// (weapon_thompson_weapon_fire_1st, ...), besides the bare <copy>... form.
inline bool CampaignDonorEvent(std::string_view copy, std::string_view event)
{
    const std::string prefix = "weapon_" + std::string(copy) + "_weapon_";
    return event.rfind(prefix, 0) == 0;
}
// The G43's (its tests).
inline bool G43DonorEvent(std::string_view weapon, std::string_view event)
{
    return weapon == "gewehr43" && CampaignDonorEvent("m1garand", event);
}

inline bool G43ClipEjectEvent(std::string_view weapon, std::string_view event)
{
    if (!G43DonorEvent(weapon, event)) return false;
    return event == "weapon_m1garand_weapon_last_shot_eject" ||
           event == "weapon_m1garand_weapon_last_shot_1st_clip_ping" ||
           event == "weapon_m1garand_weapon_last_shot_3rd_clip_ping";
}

// A campaign gun's reload: its copy's reload cue becomes its own, as the
// campaign banks have them (forest.wbk: GR_G43; mace2.wbk: GR_STEN, GR_BREN,
// GR_fg42, GR_enfield_reload, GR_Enfield_rechamber). Empty: not a reload cue
// of that gun's copy.
inline std::string CampaignReloadCue(std::string_view weapon, std::string_view upperCue)
{
    auto starts = [&](std::string_view prefix) { return upperCue.rfind(prefix, 0) == 0; };
    if (weapon == "gewehr43" && starts("GR_M1G")) return "GR_G43";
    if (weapon == "sten" && starts("GR_THOMPSON")) return "GR_STEN";
    if (weapon == "bren" && starts("GR_BAR")) return "GR_BREN";
    if ((weapon == "fg42" || weapon == "fg42_scoped") && starts("GR_BAR")) return "GR_fg42";
    // The scoped Lee-Enfield (Laison's): a copy of the scoped Kar98, whose
    // reload is in parts (GR_K98Scope_Start, _reload a round, _end); laison.wbk
    // has the scoped Enfield's own three, and mace2.wbk its bolt.
    if (weapon == "lee_enfield_sniper" && starts("GR_K98SCOPE_START")) return "GR_Enfield_Scope_start";
    if (weapon == "lee_enfield_sniper" && starts("GR_K98SCOPE_RELOAD")) return "GR_Enfield_Scope_reload";
    if (weapon == "lee_enfield_sniper" && starts("GR_K98SCOPE_END")) return "GR_Enfield_Scope_end";
    if (weapon == "lee_enfield_sniper" && starts("GF_KAR98")) return "GR_Enfield_rechamber";
    if (weapon == "lee_enfield" && starts("GF_KAR98")) return "GR_Enfield_rechamber";
    if (weapon == "lee_enfield" && starts("GR_KAR98")) return "GR_enfield_reload";
    return std::string();
}
// The kept name for the G43 (its tests).
inline std::string G43ReloadCue(std::string_view weapon, std::string_view upperCue)
{
    return weapon == "gewehr43" ? CampaignReloadCue(weapon, upperCue) : std::string();
}

inline std::string CampaignShotSuffix(std::string_view weapon, std::string suffix)
{
    // A G43 last shot uses its regular shot cue; Forest has no _CLIP cue.
    if (weapon == "gewehr43" && (suffix == "1ST_CLIP" || suffix == "3RD_CLIP"))
        suffix.resize(3);
    // The MG42's shots are the ones the game's own MG42 plays in
    // multiplayer: the Horch turret's MG42_HORCH_FIRE_1ST/_3RD (Eder Dam's
    // bank) are, byte for byte in size and length, Forest's
    // MG42_FIRE_1ST_LASTSHOT (2.58 s) and MG42_FIRE_3RD_LASTSHOT (3.06 s):
    // a shot with its ring-off, no loop flag, played once a shot. (Forest's
    // MG42_FIRE_1ST_LOOP is the mounted gun's loop, started once and stopped
    // on release: played once a shot it never stopped.)
    if (weapon == "mg42" && (suffix == "1ST" || suffix == "3RD")) suffix += "_LASTSHOT";
    return suffix;
}

// The prefix of a copy's gunshot cues: its name, but the scoped Kar98 fires
// the Kar98's (KAR98_FIRE_1ST; there is no KAR98_SNIPER_FIRE_1ST).
inline std::string CampaignCopyShotPrefix(std::string_view copy)
{
    std::string upper(copy == "kar98_sniper" ? std::string_view("kar98") : copy);
    for (char& c : upper) c = char(toupper(static_cast<unsigned char>(c)));
    return upper;
}
// A campaign gun's own gunshot cue: <shot>_FIRE_<suffix>, but the scoped
// Lee-Enfield's has only the first person's (Lee_Enfield_Sniper_FIRE_1ST,
// laison.wbk); others hear the Lee-Enfield's (LEE_ENFIELD_FIRE_3RD).
inline std::string CampaignShotCue(std::string_view weapon, std::string_view shot, const std::string& suffix)
{
    if (weapon == "lee_enfield_sniper" && suffix.rfind("3RD", 0) == 0) return "LEE_ENFIELD_FIRE_3RD";
    return std::string(shot) + "_FIRE_" + suffix;
}
