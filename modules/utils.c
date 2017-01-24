#include "utils.h"

// Source:
// https://github.com/anakod/Sming/blob/master/Sming/system/stringconversion.cpp#L93
double atof(const char* s)
{
  double result = 0;
  double factor = 1.0;

  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    ++s;

  if (*s == 0)
    return 0;

  if (*s == '-')
  {
    factor = -1.0;
    ++s;
  }
  if (*s == '+')
  {
    ++s;
  }

  BOOL decimals = false;
  char c;
  while((c = *s))
  {
    if (c == '.')
    {
      decimals = true;
      ++s;
      continue;
    }

    int d = c - '0';
    if (d < 0 || d > 9)
      break;

    result = 10.0 * result + d;
    if (decimals)
      factor *= 0.1;
    ++s;
  }

  return result * factor;
}
