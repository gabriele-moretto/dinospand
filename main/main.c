#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "gpio.h"
#include "frequency.h"
#include "esp_timer.h"
#include "ssd1306.h"
#include "bitmap_img.h"

//Debug mode
#define DEBUG

//Stati della macchina
#define INIT 0
#define WORK 1
#define ERROR 2

static uint8_t sys_state;

//Fasi del ciclo di funzionamento
#define WAIT 0
#define DELAY 1
#define DISCHARGE 2
#define CHARGE 3

//Struct unica che contiene tutte le informazioni di un oggetto
typedef struct{
    drawer_in *inputs;
    drawer_out *outputs;
    uint8_t state;
}drawer_t;

drawer_t drawer_sx = {
    .inputs = &drawer_sx_in,
    .outputs = &drawer_sx_out,
    .state = 0
};

drawer_t drawer_dx = {
    .inputs = &drawer_dx_in,
    .outputs = &drawer_dx_out,
    .state = 0
};

//Tempistiche
int delayAfterDetect; //ms
int dischargeTime; //ms
int chargeTime; //ms
int closeTime; //ms

//Gestore dei relè
void drawer_output_handler(void *pvParameters){
    drawer_t *drawer = (drawer_t*) pvParameters;
    //char task_name[configMAX_TASK_NAME_LEN] = pcTaskGetName(NULL);

    while(1){
        TickType_t last_wake_time = xTaskGetTickCount();

        switch (sys_state) {
        case INIT:
            gpio_set_level(drawer->outputs->drawer_down, 1);
            gpio_set_level(drawer->outputs->drawer_up, 1);
            break;

        case WORK:
            switch (drawer->state) {
            case WAIT:
                if(drawer->inputs->start_state || drawer->inputs->tail)
                    drawer->state++;
                break;
            
            case DELAY:
                vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(delayAfterDetect));
                gpio_set_level(drawer->outputs->drawer_down, 0);
                drawer->state++;
                break;

            case DISCHARGE:
                vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(dischargeTime));
                gpio_set_level(drawer->outputs->drawer_down, 1);

                if(drawer->inputs->read_mag_sens) {
                    if(gpio_get_level(drawer->inputs->mag_sens))
                        drawer->state++;
                } else {
                    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(closeTime));
                    gpio_set_level(drawer->outputs->drawer_up, 0);
                    drawer->state++;
                }
                break;

            case CHARGE:
                vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(chargeTime));
                gpio_set_level(drawer->outputs->drawer_up, 1);

                vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(closeTime));
                gpio_set_level(drawer->outputs->drawer_up, 0);

                drawer->state = WAIT;
                break;
            
            default:
                gpio_set_level(drawer->outputs->drawer_down, 1);
                gpio_set_level(drawer->outputs->drawer_up, 1);
                break;
            }
            break;

        case ERROR:
            gpio_set_level(drawer->outputs->drawer_down, 1);
            gpio_set_level(drawer->outputs->drawer_up, 1);
            break;
        
        default:
            gpio_set_level(drawer->outputs->drawer_down, 1);
            gpio_set_level(drawer->outputs->drawer_up, 1);
            break;
        }

        if((drawer->state =! DELAY) && (drawer->inputs->start_state))
            drawer->inputs->tail = true;

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

//Gestione della lettura degli input relativi ai cassetti
void drawer_inputs_handler(void *pvParameters) {
    drawer_in *input = (drawer_in*) pvParameters;

    while(1){
        bool poplar = gpio_get_level(input->photocell) && gpio_get_level(input->int_fot);
        bool manual = gpio_get_level(input->puls);

        if(poplar || manual)
            input->start_state = true;
        else
            input->start_state = false;

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

//Costanti per info prese dalla cabina
#define FREQ_AT_1KMH 2.166
#define FREQ_AT_2RPS 12.0

//Strutture dati delle info prese dalla cabina
input_freq pto_pos = {
    .gpio = PTO_POS_GPIO,
    .measuring = false
};

input_freq thg_speed = {
    .gpio = THG_SPEED_GPIO,
    .measuring = false
};

input_freq tg_speed = {
    .gpio = TG_SPEED_GPIO,
    .measuring = false
};

bool pto_work;

//Costanti per i servizi di interrupt
#define ISR_ON_TIME 500     //ms
#define ISR_OFF_TIME 5000   //ms

//Variabili dei valori letti dal connettore in cabina
double tg_speed_val, thg_speed_val, pto_pos_val;

//Gestione degli input del connettore in cabina
void tractor_input_handler(void *pvParameters) {
    bool enable = true;
    int64_t start_t;

    start_t = esp_timer_get_time();

    while(1) {
        tg_speed_val = tg_speed.frequency / FREQ_AT_1KMH; // km/h
        thg_speed_val = thg_speed.frequency / FREQ_AT_1KMH; // km/h
        pto_pos_val = pto_pos.frequency / FREQ_AT_2RPS; // rps

        if(gpio_get_level(PTO_WORK_GPIO) == 1)
            pto_work = true;
        else
            pto_work = false;

        //Abilito l'ISR per ISR_ON_TIME ms e disabilito per ISR_OFF_TIME ms
        if(enable && (esp_timer_get_time() - start_t) > ISR_ON_TIME) {
            gpio_set_intr_type(PTO_POS_GPIO, GPIO_INTR_DISABLE);
            gpio_set_intr_type(THG_SPEED_GPIO, GPIO_INTR_DISABLE);
            gpio_set_intr_type(TG_SPEED_GPIO, GPIO_INTR_DISABLE);
            
            enable = false;
            start_t = esp_timer_get_time();
        } else if(!enable && (esp_timer_get_time() - start_t) > ISR_OFF_TIME) {
            gpio_set_intr_type(PTO_POS_GPIO, GPIO_INTR_ANYEDGE);
            gpio_set_intr_type(THG_SPEED_GPIO, GPIO_INTR_ANYEDGE);
            gpio_set_intr_type(TG_SPEED_GPIO, GPIO_INTR_ANYEDGE);
            
            enable = true;
            start_t = esp_timer_get_time();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

//Variabili degli schermi oled
#define SC1_ADDR 0x3D
#define SC2_ADDR 0x3C

#define I2C_MASTER_FREQ 400000

SSD1306_t screen1, screen2;

void manage_screens(void *pvParameters) {
    ssd1306_clear_screen(&screen1, false);
    ssd1306_clear_screen(&screen2, false);

	ssd1306_contrast(&screen1, 0xff);
    ssd1306_contrast(&screen2, 0xff);

    while(1) {
        switch (sys_state)
        {
        case INIT:
            ssd1306_bitmaps(
                &screen1,
                screen1._width/2 - morettoimf_big.width/2, 
                0, 
                morettoimf_big.array, 
                morettoimf_big.width, 
                morettoimf_big.height, 
                false);
            ssd1306_display_text_box1(&screen1, 6, 24, "MorettoIMF", 10, 10, false, 100);

            ssd1306_display_text_box1(&screen2, 3, 30, "DinoSpand", 9, 9, false, 100);
            ssd1306_display_text_box1(&screen2, 4, 48, "v2.0", 4, 4, false, 100);
            ssd1306_display_text_box1(&screen2, 5, 24, "MorettoIMF", 10, 10, false, 100);

            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ssd1306_clear_screen(&screen1, false);
            ssd1306_clear_screen(&screen2, false);
            sys_state = WORK;
            break;
        
        case WORK:
            
            break;
        
        case ERROR:
            ssd1306_display_text_x3(&screen1, 0, "ERROR", 5, false);
            ssd1306_display_text_x3(&screen2, 0, "ERROR", 5, false);
            break;
        
        default:
            break;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

//Inizializza le variabili e carica gli elementi salvati in memoria
void var_init(void) {
    sys_state = INIT;

    drawer_sx.state = 0;
    drawer_sx.inputs->start_state = false;
    drawer_sx.inputs->tail = false;

    drawer_dx.state = 0;
    drawer_sx.inputs->start_state = false;
    drawer_sx.inputs->tail = false;

    #if CONFIG_I2C_PORT_0
        i2c_port_t i2c_num = I2C_NUM_0;
    #else
        i2c_port_t i2c_num = I2C_NUM_1;
    #endif

    i2c_master_bus_config_t i2c_mst_config = {
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.i2c_port = i2c_num,
		.scl_io_num = CONFIG_SCL_GPIO,
		.sda_io_num = CONFIG_SDA_GPIO,
		.flags.enable_internal_pullup = true,
	};
	i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

	screen1._i2c_bus_handle = bus_handle;
	i2c_device_add(&screen1, i2c_num, CONFIG_RESET_GPIO, SC1_ADDR);

	screen2._i2c_bus_handle = bus_handle;
    i2c_device_add(&screen2, i2c_num, CONFIG_RESET_GPIO, SC2_ADDR);

    ssd1306_init(&screen1, 128, 64);
    ssd1306_init(&screen2, 128, 64);

    delayAfterDetect = 1000;
    dischargeTime = 500;
    chargeTime = 500;
    closeTime = 50;
}

void io_init(void) {
    // ---- OUTPUT ----
    gpio_config_t io_conf_out = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask =
            (1ULL << drawer_sx_out.drawer_up)   |
            (1ULL << drawer_sx_out.drawer_down) |
            (1ULL << drawer_dx_out.drawer_up)   |
            (1ULL << drawer_dx_out.drawer_down),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf_out);

    // ---- INPUT ----
    gpio_config_t io_conf_in = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask =
            (1ULL << drawer_sx_in.photocell)    |
            (1ULL << drawer_sx_in.int_fot)      |
            (1ULL << drawer_sx_in.puls)         |
            (1ULL << drawer_dx_in.photocell)    |
            (1ULL << drawer_dx_in.int_fot)      |
            (1ULL << drawer_dx_in.puls)         |
            (1ULL << PTO_WORK_GPIO),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };

    gpio_config(&io_conf_in);

    gpio_config_t io_conf_int = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 
            (1ULL << PTO_POS_GPIO)              |
            (1ULL << THG_SPEED_GPIO)            |
            (1ULL << TG_SPEED_GPIO),
        .intr_type = GPIO_INTR_ANYEDGE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };

    gpio_config(&io_conf_int);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PTO_POS_GPIO, measure_frequency, (void*) &pto_pos);
    gpio_isr_handler_add(THG_SPEED_GPIO, measure_frequency, (void*) &thg_speed);
    gpio_isr_handler_add(TG_SPEED_GPIO, measure_frequency, (void*) &tg_speed);
}

void print_drawer(drawer_t drawer){
    print_drawer_input(*(drawer.inputs));
    print_drawer_output(*(drawer.outputs));

    switch (drawer.state)
    {
    case WAIT:
        printf("Stato: stop\n");
        break;
    
    case DELAY:
        printf("Stato: tempo di attesa per inizio ciclo\n");
        break;

    case DISCHARGE:
        printf("Stato: scarico concime\n");
        break;

    case CHARGE:
        printf("Stato: carico concime\n");
        break;
    
    default:
        printf("Stato: sconosciuto");
        break;
    }
}

void print_infos(void *pvParameter) {
    while(1) {
        printf("Cassetto sinistro:\n");
        print_drawer(drawer_sx);
        printf("\n-----------------------------\n");

        printf("Cassetto destro:\n");
        print_drawer(drawer_dx);
        printf("\n-----------------------------\n");

        printf("tg_speed_val = %lf km/h\n", tg_speed_val);
        printf("thg_speed_val = %lf km/h\n", thg_speed_val);
        printf("pto_pos_val = %lf rps/h\n", pto_pos_val);
        if(pto_work)
            printf("pto_work = true\n");
        else
            printf("pto_work = false\n");
        printf("\n------------------------------\n");

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    var_init();
    io_init();

    xTaskCreatePinnedToCore(drawer_inputs_handler, "gpio_sx", 1024, (void*) &drawer_sx_in, 5, NULL, 0);
    xTaskCreatePinnedToCore(drawer_inputs_handler, "gpio_dx", 1024, (void*) &drawer_dx_in, 5, NULL, 0);
    xTaskCreatePinnedToCore(drawer_output_handler, "drawer_sx", 1024, (void*) &drawer_sx, 5, NULL, 0);
    xTaskCreatePinnedToCore(drawer_output_handler, "drawer_dx", 1024, (void*) &drawer_dx, 5, NULL, 0);

    xTaskCreatePinnedToCore(tractor_input_handler, "tractor_in", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(print_infos, "printer", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(manage_screens, "screens", 8192, NULL, 5, NULL, 1);

    //for( ;; );
}
