#pragma once

#include "gca/report.hpp"

namespace gca {

inline constexpr double kDefaultComparisonTolerance = 0.01;

enum class ComparisonTrend { Neutral, Improved, Worsened };
enum class ToolPresence { Both, Added, Removed };
enum class PointChange { Modified, Inserted, Deleted };

struct ComparisonRow {
    std::string label, before, after, difference;
    ComparisonTrend trend = ComparisonTrend::Neutral;
};

struct PointDifference {
    PointChange kind;
    std::optional<std::size_t> oldLine, newLine;
};

struct ToolComparison {
    std::string tool;
    ToolPresence presence = ToolPresence::Both;
    std::size_t oldPoints = 0, newPoints = 0, differenceCount = 0;
    std::vector<PointDifference> differences;
};

struct FormatDifference {
    std::string label;
    std::vector<std::string> before, after;
};

struct ComparisonReport {
    double tolerance = kDefaultComparisonTolerance;
    std::vector<ComparisonRow> summary;
    std::vector<ToolComparison> trajectory;
    std::vector<FormatDifference> format;
    std::vector<Warning> newWarnings, resolvedWarnings;
};

[[nodiscard]] ComparisonReport compareReports(const Report& before, const Report& after,
                                              double tolerance = kDefaultComparisonTolerance);
void writeTextComparison(const Report& before, const Report& after, const ComparisonReport& comparison,
                         std::ostream& output);
void writeJsonComparison(const Report& before, const Report& after, const ComparisonReport& comparison,
                         std::ostream& output);

} // namespace gca
