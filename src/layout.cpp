#include "layout.hpp"
#include "common.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>

namespace beamer {

std::optional<Side> parse_side(std::string_view s) {
    if (s == "right") return Side::Right;
    if (s == "left") return Side::Left;
    if (s == "above" || s == "top" || s == "up") return Side::Above;
    if (s == "below" || s == "bottom" || s == "down") return Side::Below;
    return std::nullopt;
}

std::string_view side_name(Side s) {
    switch (s) {
    case Side::Right: return "right";
    case Side::Left: return "left";
    case Side::Above: return "above";
    case Side::Below: return "below";
    }
    return "right";
}

Size logical(int width, int height, double scale) {
    if (scale <= 0) scale = 1;
    return {static_cast<int>(std::lround(width / scale)), static_cast<int>(std::lround(height / scale))};
}

std::vector<Point> arrange(Size internal, const std::vector<std::pair<Size, Placement>>& externals) {
    std::vector<Point> pos{{0, 0}};
    for (const auto& [size, p] : externals) {
        switch (p.side) {
        case Side::Right: pos.push_back({internal.w, p.offset}); break;
        case Side::Left: pos.push_back({-size.w, p.offset}); break;
        case Side::Above: pos.push_back({p.offset, -size.h}); break;
        case Side::Below: pos.push_back({p.offset, internal.h}); break;
        }
    }
    int min_x = INT_MAX, min_y = INT_MAX;
    for (auto& pt : pos) min_x = std::min(min_x, pt.x), min_y = std::min(min_y, pt.y);
    for (auto& pt : pos) pt.x -= min_x, pt.y -= min_y;
    return pos;
}

static fs::path store_path() { return state_dir() / "layouts.json"; }

LayoutStore::LayoutStore() {
    std::ifstream f(store_path());
    if (!f) return;
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    for (auto& [key, v] : j.items()) {
        if (!v.is_object()) continue;
        auto side = parse_side(v.value("side", "right"));
        map_[key] = {side.value_or(Side::Right), v.value("offset", 0)};
    }
}

Placement LayoutStore::get(const std::string& key) const {
    auto it = map_.find(key);
    return it == map_.end() ? Placement{} : it->second;
}

void LayoutStore::set(const std::string& key, Placement p) { map_[key] = p; }

void LayoutStore::save() const {
    nlohmann::json j = nlohmann::json::object();
    for (auto& [key, p] : map_) j[key] = {{"side", side_name(p.side)}, {"offset", p.offset}};
    std::error_code ec;
    fs::create_directories(state_dir(), ec);
    std::ofstream(store_path(), std::ios::trunc) << j.dump(2) << '\n';
}

} // namespace beamer
