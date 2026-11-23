#include "shared/movement/movement_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace netarena {
namespace {

// Keep the surface on the safe side of the exact offset, including when the
// smaller operand would disappear in a double sum at extreme coordinates.
float offset_bound(float edge, float offset, bool upward) {
    const float rounded = static_cast<float>(static_cast<double>(edge) + offset);
    const auto [larger, smaller] = std::abs(edge) >= std::abs(offset)
        ? std::pair{edge, offset} : std::pair{offset, edge};
    const double residual = (static_cast<double>(larger) - rounded) + smaller;
    if (upward && residual > 0) return std::nextafter(rounded, std::numeric_limits<float>::infinity());
    if (!upward && residual < 0) return std::nextafter(rounded, -std::numeric_limits<float>::infinity());
    return rounded;
}

Rect offset_rect(Rect rect, float inset) {
    return {{offset_bound(rect.min.x, inset, inset > 0), offset_bound(rect.min.y, inset, inset > 0)},
            {offset_bound(rect.max.x, -inset, inset < 0), offset_bound(rect.max.y, -inset, inset < 0)}};
}

bool strictly_inside(Vec2 point, Rect rect) {
    return point.x > rect.min.x && point.x < rect.max.x &&
           point.y > rect.min.y && point.y < rect.max.y;
}

} // namespace

MovementModel::MovementModel(MapDefinition map, MovementConfig config)
    : map_(std::move(map)), config_(config) {
    if (!validate(map_.bounds).ok() ||
        !std::isfinite(config.speed) || config.speed <= 0 || config.speed > 10000 ||
        !std::isfinite(config.player_radius) || config.player_radius <= 0 || config.player_radius > 10000 ||
        config.tick_hz == 0 || config.tick_hz > 1000 || map_.map_id.empty() ||
        map_.map_id.size() > 64 || map_.spawn_points.empty() || map_.spawn_points.size() > 4 ||
        map_.obstacles.size() > 32)
        throw std::invalid_argument("invalid movement configuration");

    center_bounds_ = offset_rect(map_.bounds, config.player_radius);
    if (!validate(center_bounds_).ok()) throw std::invalid_argument("unrepresentable movement bounds");
    const auto bounds = map_.bounds;
    blocked_centers_.reserve(map_.obstacles.size());
    for (auto obstacle : map_.obstacles) {
        if (!validate(obstacle).ok() ||
            obstacle.min.x < bounds.min.x || obstacle.min.y < bounds.min.y ||
            obstacle.max.x > bounds.max.x || obstacle.max.y > bounds.max.y)
            throw std::invalid_argument("invalid obstacle");

        const auto blocked = offset_rect(obstacle, -config.player_radius);
        if (!validate(blocked).ok()) throw std::invalid_argument("unrepresentable obstacle bounds");
        blocked_centers_.push_back(blocked);
    }
    for (auto spawn : map_.spawn_points) {
        if (!valid_position(spawn)) throw std::invalid_argument("invalid spawn position");
    }
}

Result<Vec2> MovementModel::normalize_intent(Vec2 direction) const {
    if (!direction.finite())
        return Result<Vec2>::failure({ErrorCode::InvalidArgument, "nonfinite direction", "direction"});
    const double length = std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.y));
    if (length > 1) {
        direction.x = static_cast<float>(direction.x / length);
        direction.y = static_cast<float>(direction.y / length);
    }
    return Result<Vec2>::success(direction);
}

bool MovementModel::valid_position(Vec2 position) const {
    const auto bounds = center_bounds_;
    if (!position.finite() || position.x < bounds.min.x || position.x > bounds.max.x ||
        position.y < bounds.min.y || position.y > bounds.max.y)
        return false;
    return std::none_of(blocked_centers_.begin(), blocked_centers_.end(),
                        [&](Rect rect) { return strictly_inside(position, rect); });
}

MoveState MovementModel::step(const MoveState& state, Vec2 direction) const {
    if (!valid_position(state.position)) return {map_.spawn_points.front(), {}};
    if (!direction.finite() || std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.y)) > 1.000001)
        return {state.position, {}};

    Vec2 position = state.position;
    const double distance = static_cast<double>(config_.speed) / config_.tick_hz;
    // Sweep X then Y. A representable point on a blocking face is legal.
    for (bool horizontal : {true, false}) {
        const auto along = [horizontal](Vec2 point) { return horizontal ? point.x : point.y; };
        const auto across = [horizontal](Vec2 point) { return horizontal ? point.y : point.x; };
        float& at = horizontal ? position.x : position.y;
        const float cross = across(position);
        const double delta = along(direction) * distance;
        const double lo = along(center_bounds_.min);
        const double hi = along(center_bounds_.max);
        double target = std::clamp(static_cast<double>(at) + delta, lo, hi);
        for (const auto obstacle : blocked_centers_) {
            if (cross <= across(obstacle.min) || cross >= across(obstacle.max)) continue;
            const float low = along(obstacle.min);
            const float high = along(obstacle.max);
            if (delta > 0 && at <= low && target > low) target = low;
            if (delta < 0 && at >= high && target < high) target = high;
        }

        auto rounded = static_cast<float>(target);
        // A coarse float grid can otherwise round beyond this axis's swept path.
        if ((delta > 0 && rounded > target) || (delta < 0 && rounded < target))
            rounded = std::nextafter(rounded, at);
        at = rounded;
    }
    const auto vx = (static_cast<double>(position.x) - state.position.x) * config_.tick_hz;
    const auto vy = (static_cast<double>(position.y) - state.position.y) * config_.tick_hz;
    return {position, {static_cast<float>(vx), static_cast<float>(vy)}};
}

bool MovementModel::can_pickup(Vec2 player, Vec2 coin, float radius) const {
    return player.finite() && coin.finite() && std::isfinite(radius) && radius >= 0 &&
           std::hypot(static_cast<double>(player.x) - coin.x, static_cast<double>(player.y) - coin.y) <= radius;
}

} // namespace netarena
