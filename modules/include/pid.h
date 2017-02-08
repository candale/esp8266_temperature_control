/**
This is largely based on the work of br3ttb (github) for Arduino PID
https://github.com/br3ttb/Arduino-PID-Library/
**/


#include "os_type.h"


#define DEFAULT_WINDUP_GUARD 200


typedef struct {
    double k_proportioanl;
    double k_integral;
    double k_derivative;

    double integral_err;
    double last_input;

    double windup_guard;

    uint32_t sample_time;
    double setpoint;
} PID_Conf;

void PID_Init(
    PID_Conf* conf, double k_proportioanl, double k_integral,
    double k_derivative, uint32_t sample_time, double setpoint);
double PID_Compute(PID_Conf* conf, double input);
void PID_SetKProportinal(PID_Conf* conf, double k_proportioanl);
void PID_SetKIntegral(PID_Conf* conf, double k_integral);
void PID_SetKDerivative(PID_Conf* conf, double k_derivative);
