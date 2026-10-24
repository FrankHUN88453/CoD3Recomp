#include "../CoD3Host/campaign_animation.h"
#include <cassert>

int main()
{
    // The Garand has a last-shot move that the G43 archive does not contain.
    assert(CampaignAnimationName("gewehr43", "w_arms_geg43_fireLAST") == "w_arms_geg43_fire");
    assert(CampaignAnimationName("gewehr43", "w_geg43_fireLAST") == "w_geg43_fire");
    assert(CampaignAnimationName("gewehr43", "w_arms_geg43_idle_ads_a") == "w_arms_geg43_idle_ads");
    assert(CampaignAnimationName("gewehr43", "w_geg43_idle_ads") == "w_geg43_idle_ads");
    assert(CampaignAnimationName("gewehr43", "w_geg43_reload") == "w_geg43_reload");
    assert(CampaignAnimationName("fg42", "w_FG42_idle_ads_a") == "w_FG42_idle_ads_a");
    assert(CampaignAnimationName("lee_enfield", "w_brRFL_fireLAST") == "w_brRFL_fireLAST");
}
