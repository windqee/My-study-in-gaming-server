#include "shared/movement/movement_model.hpp"
#include "game_fixtures.hpp"
#include "check.hpp"
#include <limits>
using namespace netarena;
using namespace fixtures;
int main() {
    MovementModel model(map());
    CHECK(near(model.step({{2,2},{}},{1,0}).position.x,2+4.F/30));
    auto diagonal=model.normalize_intent({1,1}); CHECK(diagonal.ok());
    auto d=model.step({{2,2},{}},*diagonal.value_if());
    CHECK(near(std::hypot(d.position.x-2,d.position.y-2),4.F/30));
    CHECK(!model.normalize_intent({INFINITY,0}).ok());
    CHECK(!model.normalize_intent({NAN,0}).ok());
    CHECK(model.normalize_intent({std::numeric_limits<float>::max(),std::numeric_limits<float>::max()}).ok());
    CHECK(model.valid_position({.3F,.3F})); CHECK(!model.valid_position({.29F,2}));
    CHECK(model.can_pickup({2,2},{2.5F,2},.5F));
    CHECK(!model.can_pickup({2,2},{2.5F,2},.49F));
    CHECK(!model.can_pickup({2,2},{NAN,2},.5F));
    auto wide = map();
    wide.bounds.max.x = 5000000;
    wide.spawn_points[0] = {4000000, 2};
    const auto large = MovementModel(wide).step({{4000000, 2}, {}}, {1, 0});
    CHECK(large.velocity.x <= 4.0F);
    auto shifted = map();
    shifted.bounds = {{4000000, 0}, {4000010, 20}};
    shifted.spawn_points = {{4000002, 2}};
    shifted.obstacles = {{{4000001, 4}, {4000002, 8}}};
    const MovementModel shifted_model(shifted);
    CHECK(!shifted_model.valid_position({4000000.25F, 2}));
    CHECK(!shifted_model.valid_position({4000000.75F, 5}));
    CHECK(shifted_model.valid_position({4000000.5F, 5}));
    auto wallmap=map(); wallmap.obstacles={{{4,1},{4.01F,8}}};
    MovementModel walls(wallmap,{300,.25F,30});
    auto hit=walls.step({{2,2},{}},{1,0}); CHECK(near(hit.position.x,3.75F)); CHECK(walls.valid_position(hit.position));
    auto slide=walls.step({{3.75F,2},{}},{.6F,.8F}); CHECK(near(slide.position.x,3.75F)); CHECK(slide.position.y>2);
    auto tangent=walls.step({{2,8.25F},{}},{1,0}); CHECK(tangent.position.x>4.1F);
    const auto edge = model.step({{19.65F, 19.65F}, {}}, {.7F, .7F});
    CHECK(model.valid_position(edge.position));
    CHECK(near(edge.position.x, 19.7F));
    CHECK(near(edge.position.y, 19.7F));
    CHECK(near(model.step({{2,2},{}},{NAN,0}).position.x,2));
    CHECK(throws([&]{ MovementModel bad(map(),{0,.3F,30}); }));
    CHECK(throws([&]{ MovementModel bad(map(),{4,-1,30}); }));
    CHECK(throws([&]{ MovementModel bad(map(),{4,0,30}); }));
    CHECK(throws([&]{ MovementModel bad(map(),{10001,.3F,30}); }));
    CHECK(throws([&]{ MovementModel bad(map(),{4,.3F,1001}); }));
    CHECK(throws([&]{ MovementModel bad(map(),{4,.3F,0}); }));
    CHECK(throws([&]{ auto m=map();m.bounds.max.x=NAN;MovementModel bad(m); }));
    CHECK(throws([&]{ auto m=map();m.spawn_points[0]={0,0};MovementModel bad(m); }));
    CHECK(throws([&]{ auto m=map();m.obstacles={{{4,4},{3,3}}};MovementModel bad(m); }));
}
