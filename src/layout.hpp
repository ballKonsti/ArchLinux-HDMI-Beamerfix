#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beamer {

enum class Side { Right, Left, Above, Below };

std::optional<Side> parse_side(std::string_view s);
std::string_view side_name(Side s);

// Where an external screen sits relative to the laptop panel.
// offset: logical pixels along the shared edge, measured from the laptop's
// top edge (left/right) or left edge (above/below). May be negative.
struct Placement {
    Side side = Side::Right;
    int offset = 0;
};

struct Size {
    int w = 0, h = 0;
};
struct Point {
    int x = 0, y = 0;
};

// Pixel size divided by scale, the way compositors compute layout size.
Size logical(int width, int height, double scale);

// Returns positions for [internal, externals...], shifted so that the
// smallest x and y are 0 (some compositors dislike negative coordinates).
std::vector<Point> arrange(Size internal, const std::vector<std::pair<Size, Placement>>& externals);

// Remembers placements per display (keyed by its description) in
// ~/.local/state/beamer/layouts.json, so a projector keeps its spot.
class LayoutStore {
public:
    LayoutStore();
    Placement get(const std::string& key) const;
    void set(const std::string& key, Placement p);
    void save() const;

private:
    std::unordered_map<std::string, Placement> map_;
};

} // namespace beamer
