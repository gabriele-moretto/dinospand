#include "driver/gpio.h"

typedef struct{
    gpio_num_t gpio;

    bool measuring;

    int64_t t_start;
    int64_t t_falling;
    int64_t t_stop;
    
    int64_t period;
    double frequency;
    double duty_cycle;
} input_freq;

void measure_frequency (void *arg);