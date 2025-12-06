#include "gpio.h"
#include "esp_intr_alloc.h"
#include "frequency.h"


void print_drawer_input(drawer_in input){
    if(!gpio_get_level(input.int_fot))
        printf("Lettura fotocellula: ON   |");
    else
        printf("Lettura fotocellula: OFF   ");

    if(input.tail)
        printf("   Coda: ON\n");
    else
        printf("   Coda: OFF\n");
}

void print_drawer_output(drawer_out output){
    if(gpio_get_level(output.drawer_up))
        printf("Cassetto sopra: aperto   |");
    else
        printf("Cassetto sopra: chiuso   |");
    
    if(gpio_get_level(output.drawer_down))
        printf("   Cassetto sotto: aperto\n");
    else
        printf("   Cassetto sotto: chiuso\n");
}