#pragma once

namespace ppcb {

// Version of the program, "X.Y.Z" from build_number.txt.
const char *appVersion();
// The day of the build, "YYYY-MM-DD" from build_number.txt; empty when it
// has none.
const char *appBuildDate();

} // namespace ppcb
