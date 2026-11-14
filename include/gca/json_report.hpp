#pragma once

#include "gca/report.hpp"

namespace gca {

/// Schema 1, UTF-8 without BOM, fixed four-decimal numbers and JSON escaping.
void writeJsonReport(const Report& report, std::ostream& out);
[[nodiscard]] std::string renderJsonReport(const Report& report);

} // namespace gca
