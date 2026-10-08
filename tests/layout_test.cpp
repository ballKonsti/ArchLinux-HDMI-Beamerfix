#include "layout.hpp"

#include <cstdio>
#include <print>

using namespace beamer;

static int failures = 0;

static void expect(const char* what, std::vector<Point> got, std::vector<Point> want) {
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < got.size(); ++i) ok = got[i].x == want[i].x && got[i].y == want[i].y;
    if (ok) return;
    ++failures;
    std::print(stderr, "FAIL {}: got", what);
    for (auto& p : got) std::print(stderr, " ({},{})", p.x, p.y);
    std::print(stderr, ", want");
    for (auto& p : want) std::print(stderr, " ({},{})", p.x, p.y);
    std::println(stderr, "");
}

int main() {
    // 1920x1080 laptop at 1.25 -> 1536x864 logical
    Size lap = logical(1920, 1080, 1.25);
    if (lap.w != 1536 || lap.h != 864) ++failures, std::println(stderr, "FAIL logical: {}x{}", lap.w, lap.h);

    Size proj = logical(1024, 768, 1.0);
    Size uhd = logical(3840, 2160, 1.5); // 2560x1440

    expect("right", arrange(lap, {{proj, {Side::Right, 0}}}), {{0, 0}, {1536, 0}});
    expect("left", arrange(lap, {{proj, {Side::Left, 0}}}), {{1024, 0}, {0, 0}});
    expect("above", arrange(lap, {{proj, {Side::Above, 0}}}), {{0, 768}, {0, 0}});
    expect("below", arrange(lap, {{proj, {Side::Below, 0}}}), {{0, 0}, {0, 864}});

    // centered above: offset (1536 - 1024) / 2 = 256
    expect("above centered", arrange(lap, {{proj, {Side::Above, 256}}}), {{0, 768}, {256, 0}});
    // 4K to the right, bottom-aligned: offset 864 - 1440 = -576 -> laptop shifted down
    expect("right bottom-aligned 4K", arrange(lap, {{uhd, {Side::Right, -576}}}), {{0, 576}, {1536, 0}});
    // two screens on both sides
    expect("left + right", arrange(lap, {{proj, {Side::Left, 0}}, {uhd, {Side::Right, 0}}}),
           {{1024, 0}, {0, 0}, {2560, 0}});

    auto side = parse_side("top");
    if (!side || *side != Side::Above || parse_side("nowhere")) ++failures, std::println(stderr, "FAIL parse_side");

    if (failures) return 1;
    std::println("layout: all tests passed");
}
