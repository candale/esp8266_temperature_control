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
void ftoa_(char* buff, float number) {
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


void ftoa(char* buff, double val){
    uint8_t precision = 3;
    char buff_t[10];
    os_sprintf(buff, "%d", (int)val);
    if(precision > 0) {
        os_strcat(buff, ".");
        uint32_t frac;
        uint32_t mult = 1;
        uint8_t padding = precision -1;
        while(precision--)
            mult *=10;

        if(val >= 0)
            frac = (val - (int)val) * mult;
        else
            frac = ((int)val - val ) * mult;
        uint32_t frac1 = frac;
        while( frac1 /= 10 )
            padding--;
        while(  padding--)
            os_strcat(buff, "0");
        os_sprintf(buff_t, "%d", frac);
        os_strcat(buff, buff_t);
    }
}
