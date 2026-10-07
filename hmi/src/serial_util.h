#pragma once
#include <stddef.h>

// Increments the last group of digits in a serial number, keeping leading
// zeros: "EXP2410034" -> "EXP2410035", "A-0099" -> "A-0100",
// "AB12CD" -> "AB13CD", "999" -> "1000".
// A serial with no digits is copied unchanged and false is returned.
bool serial_increment(const char *in, char *out, size_t out_size);
