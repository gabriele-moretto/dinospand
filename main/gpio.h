#include "driver/gpio.h"

#define RELAY1_GPIO 21
#define RELAY2_GPIO 47
#define RELAY3_GPIO 48
#define RELAY4_GPIO 37

#define INTSX_GPIO 4
#define INTDX_GPIO 5

#define PULSX_GPIO 6
#define PULDX_GPIO 7

#define SDA_GPIO 15
#define SCL_GPIO 16

#define FOTSX_GPIO 10
#define FOTDX_GPIO 11

#define MAGNETSX_GPIO 12
#define MAGNETDX_GPIO 13

#define PTO_POS_GPIO 38
#define PTO_WORK_GPIO 9

#define TG_SPEED_GPIO 36
#define THG_SPEED_GPIO 35

typedef struct {
    gpio_num_t sda;
    gpio_num_t scl;
}i2c_pins_t;

typedef struct {
    gpio_num_t drawer_up;
    gpio_num_t drawer_down;
}drawer_out;

typedef struct {
    gpio_num_t photocell;
    gpio_num_t int_fot;
    gpio_num_t puls;
    gpio_num_t mag_sens;

    bool start_state;
    bool tail;
    bool read_mag_sens;
}drawer_in;

static i2c_pins_t i2c0 = {
    .sda = SDA_GPIO,
    .scl = SCL_GPIO
};

static drawer_out drawer_sx_out = {
    .drawer_down = RELAY1_GPIO,
    .drawer_up = RELAY2_GPIO
};

static drawer_out drawer_dx_out = {
    .drawer_down = RELAY3_GPIO,
    .drawer_up = RELAY4_GPIO
};

static drawer_in drawer_sx_in = {
    .photocell = FOTSX_GPIO,
    .int_fot = INTSX_GPIO,
    .puls = PULSX_GPIO,
    .mag_sens = MAGNETSX_GPIO
};

static drawer_in drawer_dx_in = {
    .photocell = FOTDX_GPIO,
    .int_fot = INTDX_GPIO,
    .puls = PULDX_GPIO,
    .mag_sens = MAGNETDX_GPIO
};

void print_drawer_input(drawer_in input);
void print_drawer_output(drawer_out output);