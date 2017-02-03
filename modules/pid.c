#include "pid.h"


void PID_Init(
        PID_Conf* conf, double k_proportioanl, double k_integral,
        double k_derivative, uint32_t sample_time, double setpoint) {
    conf->k_proportioanl = k_proportioanl;
    conf->k_integral = k_integral;
    conf->k_derivative = k_derivative;
    // Sample time in seconds
    conf->sample_time = sample_time;
    conf->setpoint = setpoint;

    conf->integral_err = 0;

    /**
    Usually you would compute the derivative of the temperature with this formula:

    derivative =        current_temp - last_temp
                 --------------------------------------
                  current_sample_time - last_sample_time

    If we already know the sample time we can put that sample time in
    k_derivative so we don't have to make the extra calculation at each sample
    time.
    This would look like this:

    derivative = k_derivative *       1      * (current_temp - last_temp)
                                 -----------
                                 sample_time

    So we divide k_derivative to the sample time and multiple k_integral
    to the sample time so that the added error also keeps track of time.
    **/
    conf->k_derivative = conf->k_derivative / sample_time;
    conf->k_integral = conf->k_integral * sample_time;
}


double PID_Compute(PID_Conf* conf, double input) {
    double error = conf->setpoint - input;

    conf->integral_err += error;
    double derivative = conf->k_derivative * error;

    return (
        conf->k_proportioanl * error +
        conf->k_integral * error +
        derivative
    );
}


void PID_Setpoint(PID_Conf* conf, double setpoint) {
    conf->setpoint = setpoint;
}


void PID_SetKProportinal(PID_Conf* conf, double k_proportioanl) {
    conf->k_proportioanl = k_proportioanl;
}

void PID_SetKIntegral(PID_Conf* conf, double k_integral) {
    conf->k_integral = k_integral * conf->sample_time;
}

void PID_SetKDerivative(PID_Conf* conf, double k_derivative) {
    conf->k_derivative = k_derivative / conf->sample_time;
}
