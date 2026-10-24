#include "../CoD3Host/campaign_sound.h"
#include <cassert>

int main()
{
    assert(G43DonorEvent("gewehr43", "weapon_m1garand_weapon_fire_1st"));
    assert(!G43DonorEvent("gewehr43", "footstep_walk_mud"));
    assert(!G43DonorEvent("fg42", "weapon_m1garand_weapon_fire_1st"));
    assert(G43ClipEjectEvent("gewehr43", "weapon_m1garand_weapon_last_shot_1st_clip_ping"));
    assert(G43ClipEjectEvent("gewehr43", "weapon_m1garand_weapon_last_shot_eject"));
    assert(!G43ClipEjectEvent("gewehr43", "weapon_m1garand_weapon_last_shot_1st"));
    assert(!G43ClipEjectEvent("m1garand", "weapon_m1garand_weapon_last_shot_eject"));
    assert(CampaignShotSuffix("gewehr43", "1ST_CLIP") == "1ST");
    assert(CampaignShotSuffix("gewehr43", "3RD_CLIP") == "3RD");
    assert(CampaignShotSuffix("gewehr43", "1ST") == "1ST");
    assert(CampaignShotSuffix("m1garand", "1ST_CLIP") == "1ST_CLIP");
    assert(CampaignShotSuffix("mg42", "1ST") == "1ST_LASTSHOT");
    assert(CampaignShotSuffix("mg42", "3RD") == "3RD_LASTSHOT");
    assert(G43ReloadCue("gewehr43", "GR_M1G") == "GR_G43");
    assert(G43ReloadCue("gewehr43", "GR_M1G_EMPTY") == "GR_G43");
    assert(G43ReloadCue("gewehr43", "M1GARAND_FIRE_1ST").empty());
    assert(G43ReloadCue("m1garand", "GR_M1G").empty());
    assert(CampaignReloadCue("sten", "GR_THOMPSON") == "GR_STEN");
    assert(CampaignReloadCue("bren", "GR_BAR") == "GR_BREN");
    assert(CampaignReloadCue("fg42_scoped", "GR_BAR") == "GR_fg42");
    assert(CampaignReloadCue("lee_enfield", "GR_KAR98") == "GR_enfield_reload");
    assert(CampaignReloadCue("lee_enfield", "GF_KAR98_RECHAMBER") == "GR_Enfield_rechamber");
    assert(CampaignReloadCue("thompson", "GR_THOMPSON").empty());
    assert(CampaignReloadCue("sten", "THOMPSON_FIRE_1ST").empty());
    assert(CampaignReloadCue("lee_enfield_sniper", "GR_K98SCOPE_START") == "GR_Enfield_Scope_start");
    assert(CampaignReloadCue("lee_enfield_sniper", "GR_K98SCOPE_RELOAD") == "GR_Enfield_Scope_reload");
    assert(CampaignReloadCue("lee_enfield_sniper", "GR_K98SCOPE_END") == "GR_Enfield_Scope_end");
    assert(CampaignReloadCue("lee_enfield_sniper", "GF_KAR98_RECHAMBER") == "GR_Enfield_rechamber");
    assert(CampaignReloadCue("lee_enfield", "GR_K98SCOPE_START").empty());
    assert(CampaignCopyShotPrefix("kar98_sniper") == "KAR98");
    assert(CampaignCopyShotPrefix("m1garand") == "M1GARAND");
    assert(CampaignShotCue("lee_enfield_sniper", "Lee_Enfield_Sniper", "1ST") == "Lee_Enfield_Sniper_FIRE_1ST");
    assert(CampaignShotCue("lee_enfield_sniper", "Lee_Enfield_Sniper", "3RD") == "LEE_ENFIELD_FIRE_3RD");
    assert(CampaignShotCue("sten", "STEN", "3RD") == "STEN_FIRE_3RD");
    assert(CampaignDonorEvent("thompson", "weapon_thompson_weapon_fire_1st"));
    assert(CampaignDonorEvent("bar", "weapon_bar_weapon_reload_empty"));
    assert(!CampaignDonorEvent("bar", "weapon_bazooka_weapon_fire_1st"));
    assert(!CampaignDonorEvent("thompson", "footstep_run_dirt"));
}
