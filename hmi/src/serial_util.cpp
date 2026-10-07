#include "serial_util.h"
#include <ctype.h>
#include <string.h>

bool serial_increment(const char *in, char *out, size_t out_size) {
  size_t len = strlen(in);
  if (len + 2 > out_size) return false;  // room for a carry digit + NUL

  int last = -1;
  for (int i = (int)len - 1; i >= 0; i--) {
    if (isdigit((unsigned char)in[i])) {
      last = i;
      break;
    }
  }
  if (last < 0) {
    memcpy(out, in, len + 1);
    return false;
  }
  int first = last;
  while (first > 0 && isdigit((unsigned char)in[first - 1])) first--;

  char digits[64];
  int n = last - first + 1;
  if (n >= (int)sizeof(digits) - 1) return false;
  memcpy(digits, in + first, n);
  digits[n] = '\0';

  int i = n - 1;
  while (i >= 0) {
    if (digits[i] == '9') {
      digits[i] = '0';
      i--;
    } else {
      digits[i]++;
      break;
    }
  }

  size_t pos = 0;
  memcpy(out, in, first);
  pos = first;
  if (i < 0) out[pos++] = '1';  // all nines: "99" -> "100"
  memcpy(out + pos, digits, n);
  pos += n;
  strcpy(out + pos, in + last + 1);
  return true;
}
