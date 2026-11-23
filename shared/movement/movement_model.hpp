#pragma once
#include "shared/contracts/messages.hpp"
namespace netarena {
class MovementModel {
public:
    MovementModel(MapDefinition map, MovementConfig config = {});
    [[nodiscard]] Result<Vec2> normalize_intent(Vec2 direction) const;
    // Requires a legal position and normalized direction. Defensively reset an
    // invalid position to the first spawn and treat invalid intent as neutral.
    [[nodiscard]] MoveState step(const MoveState& state, Vec2 direction) const;
    [[nodiscard]] bool valid_position(Vec2 position) const;
    [[nodiscard]] bool can_pickup(Vec2 player, Vec2 coin, float radius) const;
    [[nodiscard]] const MapDefinition& map() const noexcept { return map_; }
    [[nodiscard]] const MovementConfig& config() const noexcept { return config_; }
private:
    MapDefinition map_;
    MovementConfig config_;
    Rect center_bounds_;
    std::vector<Rect> blocked_centers_;
};
} // namespace netarena
