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

void insert_char(char* buff, char character, int position) {
    int i;
    char last_char = 0;
    char set_char = character;
    for(i = position; i <= os_strlen(buff); i++) {
        last_char = buff[i];
        buff[i] = set_char;
        set_char = last_char;
    }
}


/**
Dumb version of ftoa
**/
void ftoa(char* buff, float number) {
    int precision = 3, i;
    float beefed_number = number;
    for(i = 0; i < precision; i++) {
        beefed_number *= 10;
    }

    os_sprintf(buff, "%d", (int)beefed_number);

    insert_char(buff, '.', os_strlen(buff) - precision);
    if(number > 0 && number < 1) {
        insert_char(buff, '0', 0);
    }

    if(number > -1 && number < 0) {
        insert_char(buff, '0', 1);
    }
}
