// ride_badges.h: the launcher's side of the ride badges (include/RideBadges.h): which rides have a "GOLD" or a "PS1"
// tag on the Buy Ride panel, worked out before the game starts from the rides laid over the game folder. Only with the
// port's fixes.
#pragma once

#ifdef TPW_PORT_FIX

#include <filesystem>
#include <string>
#include <vector>

#include "content_overlay.h"
#include "gold_rides.h"

namespace tpw {

// The Gold rides of `rides` that the game folder `gameDir` has no copy of (the 18 of the 35 the base game lacks), by
// their RideBadgeKey: a ride is one when gameDir/<folder>/<name> is not there (each name matched ignoring case, as the
// game's own lookups are). A Gold copy of a base game ride (the 17 others) has no tag, and with no `gameDir` no ride has.
std::vector<std::string> GoldExtraRideKeys(const std::vector<GoldRide>& rides, const std::filesystem::path& gameDir);

// The keys (each one's folder) of the content rides of `rides` that come from one of the content folders `roots`: the
// PlayStation rides are those of the content folder of the mod that switched the ps1_badge hook on, so another content
// mod's rides have no tag.
std::vector<std::string> ContentRideKeys(const std::vector<ContentRide>& rides,
                                         const std::vector<std::filesystem::path>& roots);

// The launcher's "Show Ride Badges" from launcher.ini's ride_badges value: on unless it is "0", so on by default (the maintainer's
// approved exception to settings off by default, 2026-11-08).
inline bool RideBadgesSetting(const std::string& value) { return value != "0"; }

// Fills the game's lists (gRideBadgeGold from GoldRides() less what `gameDir` has, gRideBadgePs1 from ContentRides() of
// the content folders `ps1Roots`) and sets its switch to `show`: the launcher's call, after it has laid the Gold and
// content rides over the game folder and switched the mods' hooks on. Off empties the lists.
void UseRideBadges(bool show, const std::filesystem::path& gameDir, const std::vector<std::filesystem::path>& ps1Roots);

}  // namespace tpw

#endif  // TPW_PORT_FIX
