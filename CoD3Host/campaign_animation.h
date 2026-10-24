#pragma once

#include <string>
#include <string_view>
#include <utility>

// Apply to both weapon-field names and animation-table hash targets.
inline std::string CampaignAnimationName(std::string_view weapon, std::string name)
{
    if (weapon != "gewehr43") return name;
    for (const auto& pair : { std::pair<std::string_view, std::string_view>{"_idle_ads_a", "_idle_ads"},
                            {"_fireLAST", "_fire"} })
    {
        if (name.size() >= pair.first.size() &&
            name.compare(name.size() - pair.first.size(), pair.first.size(), pair.first) == 0)
        {
            name.replace(name.size() - pair.first.size(), pair.first.size(), pair.second);
            break;
        }
    }
    return name;
}
