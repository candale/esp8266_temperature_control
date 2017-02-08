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


/**
Dumb version of ftoa. It woks for now
**/
void ftoa(char* buff, float value) {
    char decimal[5];
    int precision = 3;
    int decimal_count = 0;
    os_sprintf(buff, "%d", (int)value);

    if(((int)value) == value) {
        return;
    }

    os_strcat(buff, ".");

    float fractional = value - (int)value;
    fractional = fractional < 0 ? -1 * fractional : fractional;

    while(fractional < 1 && decimal_count < precision) {
        fractional = fractional * 10;
        os_sprintf(decimal, "%s", "0");
        os_strcat(buff, decimal);
        decimal_count++;
    }

    if(decimal_count < precision) {
        os_sprintf(decimal, "%d", (int)fractional * (precision - decimal_count));
        os_strcat(buff, decimal);
    }
}

