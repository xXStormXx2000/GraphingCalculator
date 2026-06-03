#include "Grapher.h"

#include <cmath>
#include <vector>

namespace calc {

// Frontend lives in calc; pull backend names in from calc::core.
using namespace core;

namespace {

bool signChange(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return false;
    return (a > 0.0) != (b > 0.0);
}

}  // namespace

Result<std::string> renderGraph(const GraphRequest& req) {
    if (req.xName == req.yName) {
        return Diagnostic{DiagCode::AxisNamesMustDiffer, {}};
    }
    if (req.view.xLeft >= req.view.xRight) {
        return Diagnostic{DiagCode::EmptyXRange, {}};
    }
    if (req.view.yBottom >= req.view.yTop) {
        return Diagnostic{DiagCode::EmptyYRange, {}};
    }

    constexpr int kWidth  = 80;
    constexpr int kHeight = 35;
    constexpr std::size_t kStride = static_cast<std::size_t>(kWidth) + 1;

    const double xScale = kWidth  / (req.view.xRight  - req.view.xLeft);
    const double yScale = kHeight / (req.view.yTop    - req.view.yBottom);

    // One scratch stack reused for every sample.
    std::vector<double> scratch;
    scratch.reserve(req.functor.requiredStackSize());

    // Sample the function on a grid offset by half a cell.
    std::vector<double> samples(kStride * (static_cast<std::size_t>(kHeight) + 1));
    for (int j = 0; j <= kHeight; ++j) {
        for (int i = 0; i <= kWidth; ++i) {
            const double wx = (i - 0.5) / xScale + req.view.xLeft;
            const double wy = (j - 0.5) / yScale + req.view.yBottom;
            samples[static_cast<std::size_t>(i) +
                    static_cast<std::size_t>(j) * kStride] =
                req.functor({wx, wy}, scratch);
        }
    }

    // Initialise an empty canvas with newlines at row ends.
    std::string canvas(kStride * (static_cast<std::size_t>(kHeight) + 1), ' ');
    for (int j = 0; j <= kHeight; ++j) {
        canvas[(static_cast<std::size_t>(j) + 1) * kStride - 1] = '\n';
    }

    auto put = [&](int col, int row, char ch) {
        if (col < 0 || col >= kWidth)  return;
        if (row < 0 || row > kHeight)  return;
        canvas[static_cast<std::size_t>(kHeight - row) * kStride +
               static_cast<std::size_t>(col)] = ch;
    };

    // Axes.
    const int originCol = static_cast<int>(-req.view.xLeft  * xScale);
    const int originRow = static_cast<int>(-req.view.yBottom * yScale);
    if (0 <= originCol && originCol < kWidth) {
        for (int j = 0; j <= kHeight; ++j) put(originCol, j, '|');
    }
    if (0 <= originRow && originRow <= kHeight) {
        for (int i = 0; i < kWidth; ++i) put(i, originRow, '-');
    }
    if (0 <= originCol && originCol < kWidth &&
        0 <= originRow && originRow <= kHeight) {
        put(originCol, originRow, '+');
    }

    // Plot: draw '#' wherever any pair of the four corner samples changes sign.
    for (int j = 0; j < kHeight; ++j) {
        for (int i = 0; i < kWidth; ++i) {
            const std::size_t base = static_cast<std::size_t>(i) +
                                     static_cast<std::size_t>(j) * kStride;
            const double a = samples[base];
            const double b = samples[base + 1];
            const double c = samples[base + kStride];
            const double d = samples[base + kStride + 1];
            if (signChange(a, b) || signChange(a, c) ||
                signChange(d, b) || signChange(d, c) ||
                signChange(a, d) || signChange(b, c)) {
                put(i, j, '#');
            }
        }
    }

    // Axis labels.
    if (0 <= originCol && originCol < kWidth) {
        for (std::size_t i = 0;
             i < req.yName.size() &&
             originCol + 1 + static_cast<int>(i) < kWidth; ++i) {
            put(originCol + 1 + static_cast<int>(i),
                kHeight, req.yName[i]);
        }
    }
    if (0 <= originRow && originRow <= kHeight) {
        for (std::size_t i = 0;
             i < req.xName.size() &&
             static_cast<int>(i) < kWidth; ++i) {
            put(kWidth - 1 - static_cast<int>(req.xName.size()) +
                    static_cast<int>(i),
                originRow + 1, req.xName[i]);
        }
    }

    return canvas;
}

}  // namespace calc
