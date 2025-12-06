#include "frequency.h"
#include "esp_timer.h"
#include "esp_attr.h"

void IRAM_ATTR measure_frequency (void *arg) {
    input_freq *input = (input_freq*) arg;

    if(gpio_get_level(input->gpio) == 1) {
        if(input->measuring) {
            input->t_stop = esp_timer_get_time();
            input->measuring = false;

            input->period = input->t_stop - input->t_start;
            input->frequency = 1 / input->period;
            input->duty_cycle = ((input->t_falling - input->t_start) / input->period);
        } else {
            input->t_start = esp_timer_get_time();
            input->measuring = true;
        }
    } else {
        if(input->measuring)
            input->t_falling = esp_timer_get_time();
        else
            return;
    }
}