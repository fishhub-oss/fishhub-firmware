#pragma once

#include <stddef.h>

// Returns true if version string `a` is strictly greater than `b`.
// Both must be "X.Y.Z" format. Non-conforming strings compare as equal (returns false).
inline bool semverGreater(const char* a, const char* b) {
    int aMaj = 0, aMin = 0, aPat = 0;
    int bMaj = 0, bMin = 0, bPat = 0;
    if (sscanf(a, "%d.%d.%d", &aMaj, &aMin, &aPat) != 3) return false;
    if (sscanf(b, "%d.%d.%d", &bMaj, &bMin, &bPat) != 3) return false;
    if (aMaj != bMaj) return aMaj > bMaj;
    if (aMin != bMin) return aMin > bMin;
    return aPat > bPat;
}
