


#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h" 
#include "inc/ssd1306.h"
#include "inc/font.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "hardware/timer.h"
#include "pico/bootrom.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define LED_GREEN 11
#define LED_BLUE  12
#define BUZZER 21
char c='n';
ssd1306_t ssd; // Inicializa a estrutura do display

#define JOYSTICK_X_PIN 26  // GPIO para eixo X
#define JOYSTICK_Y_PIN 27  // GPIO para eixo Y
#define JOYSTICK_PB 22 // GPIO para botão do Joystick
#define BUTTON_A 5 // GPIO para botão A
#define BUTTON_B 6


#define IS_RGBW false
#define NUM_PIXELS 25
#define WS2812_PIN 7
#define LED_PISCA 13
#define tempo 100


const uint16_t WRAP_PERIOD_FAN = 54; //valor máximo do contador - WRAP
const uint16_t WRAP_PERIOD_SPRINKLER = 1000;
const uint16_t WRAP_PERIOD_BUZZER = 3000;
const float PWM_DIVISER = 1; //divisor do clock para o PWM

struct repeating_timer timer; //timer ventilador
struct repeating_timer timer2;//timer sprinkler
struct repeating_timer timer3;//timer buzzer

int sprinkler_control = 0;
int buzzer_control = 0;
bool sprinkler_active = true;
bool fan_active = false;   //ativa/desativa animação na matriz indicando ventilador ligado
bool buzzer_active = false;
int fan_animation_control; //controla frames da animação na matriz indicando ventilador ligado

// Variável global para armazenar a cor (Entre 0 e 255 para intensidade)
uint8_t led_r = 0; // Intensidade do vermelho
uint8_t led_g = 0; // Intensidade do verde
uint8_t led_b = 1; // Intensidade do azul


static volatile uint a = 1;
static volatile uint32_t last_time = 0; // Armazena o tempo do último evento (em microssegundos)

int sprinkler_level = 10;
bool up = 1;

// Prototipação da função de interrupção
static void gpio_irq_handler(uint gpio, uint32_t events);

// Buffer para desligar leds da matriz
bool desliga_led[NUM_PIXELS] = {
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0
};

//frames da animação que indicará os ventiladores ligados
bool frame_1[NUM_PIXELS] = {
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 
    0, 0, 1, 0, 0, 
    0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0
};

bool frame_2[NUM_PIXELS] = {
    0, 0, 0, 0, 0, 
    0, 0, 1, 0, 0, 
    0, 1, 0, 1, 0, 
    0, 0, 1, 0, 0, 
    0, 0, 0, 0, 0
};

bool frame_3[NUM_PIXELS] = {
    0, 0, 1, 0, 0, 
    0, 0, 0, 0, 0, 
    1, 0, 0, 0, 1, 
    0, 0, 0, 0, 0, 
    0, 0, 1, 0, 0
};



static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}


void set_one_led(uint8_t r, uint8_t g, uint8_t b, bool matriz[])
{
    // Define a cor com base nos parâmetros fornecidos
    uint32_t color = urgb_u32(r, g, b);

    // Define todos os LEDs com a cor especificada
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        if (matriz[i])
        {
            put_pixel(color); // Liga o LED com um no buffer
        }
        else
        {
            put_pixel(0);  // Desliga os LEDs com zero no buffer
        }
    }
}


void pwm_setup_buzzer(){

    gpio_set_function(BUZZER, GPIO_FUNC_PWM); //habilitar o pino GPIO como PWM

    uint slice = pwm_gpio_to_slice_num(BUZZER); //obter o canal PWM da GPIO

    pwm_set_clkdiv(slice, 4); //define o divisor de clock do PWM

    pwm_set_wrap(slice, WRAP_PERIOD_BUZZER); //definir o valor de wrap

    pwm_set_enabled(slice, true); //habilita o pwm no slice correspondente

    pwm_set_gpio_level(BUZZER,0);
    

}



void pwm_setup_fan()
{
    gpio_set_function(LED_BLUE, GPIO_FUNC_PWM); //habilitar o pino GPIO como PWM

    uint slice = pwm_gpio_to_slice_num(LED_BLUE); //obter o canal PWM da GPIO

    pwm_set_clkdiv(slice, PWM_DIVISER); //define o divisor de clock do PWM

    pwm_set_wrap(slice, WRAP_PERIOD_FAN); //definir o valor de wrap

    pwm_set_enabled(slice, true); //habilita o pwm no slice correspondente
}

void pwm_setup_sprinkler()
{
    gpio_set_function(LED_PISCA, GPIO_FUNC_PWM); //habilitar o pino GPIO como PWM

    uint slice = pwm_gpio_to_slice_num(LED_PISCA); //obter o canal PWM da GPIO

    pwm_set_clkdiv(slice, PWM_DIVISER); //define o divisor de clock do PWM

    pwm_set_wrap(slice, WRAP_PERIOD_SPRINKLER); //definir o valor de wrap

    pwm_set_enabled(slice, true); //habilita o pwm no slice correspondente
}



void pwm_fan_ctrl(uint16_t analog_input){

  
  uint16_t fan_level = analog_input;
  if(analog_input > 40) // ventiladores são ativados caso a temperatura > 40 celsius
  pwm_set_gpio_level(LED_BLUE,fan_level);
  else
  pwm_set_gpio_level(LED_BLUE,0);
  
  
  }

//função para aumetar gradualmente o brilho do led vermelho, que vai simular o sprinkler funcionando
void pwm_sprinkler_ctrl(){

printf("%d\n",sprinkler_level);

pwm_set_gpio_level(LED_PISCA,sprinkler_level);

if(up)
{

sprinkler_level+= 50;
if(sprinkler_level >= WRAP_PERIOD_SPRINKLER )// brilho alterna entre aumentar e diminuir
up = 0; 

}

else
{
    sprinkler_level-=50;
    if (sprinkler_level <= 50)
    up = 1;
}





} 

bool buzzer_callback(struct repeating_timer *t){

    if(buzzer_active)
    {

    buzzer_control+=500;

    if(buzzer_control<1000)
    pwm_set_gpio_level(BUZZER,0);

    else if(buzzer_control >= 1000 && buzzer_control <= 1500)
    pwm_set_gpio_level(BUZZER,2000);

    else
    buzzer_control = 0;

    }

    else
    pwm_set_gpio_level(BUZZER,0);

    return true;
}

bool sprinkler_callback(struct repeating_timer *t){

    if(sprinkler_active)// verifica se os sprinklers não estão desativados. Caso sim, a rotina n ocorre
    {

    sprinkler_control+= 50;

    if(sprinkler_control < 3000)
    {
    return true;
    }

    else
    {

    pwm_sprinkler_ctrl(); // ajusta o nível do led que simula o sprinkler
    
    if(sprinkler_control >= 9000)
    {
    sprinkler_control = 0;
    pwm_set_gpio_level(LED_PISCA,0);
    }

    } 

    }

    return true;
}

bool fan_animation_callback(struct repeating_timer *t){
    if(fan_active)
    {
        fan_animation_control++;
        if(fan_animation_control%3 == 1)
        set_one_led(led_r,led_g,led_b,frame_1);

        else if(fan_animation_control%3 == 2)
        set_one_led(led_r,led_g,led_b,frame_2);

        else
        set_one_led(led_r,led_g,led_b,frame_3);

    }

    else
    set_one_led(led_r,led_g,led_b,desliga_led);

    return true;


}

void setup_gpio(){

    gpio_init(LED_GREEN);              
    gpio_set_dir(LED_GREEN, GPIO_OUT); 
  
    gpio_init(LED_BLUE);              
    gpio_set_dir(LED_BLUE, GPIO_OUT); 
  
    gpio_init(LED_PISCA);              
    gpio_set_dir(LED_PISCA, GPIO_OUT); 

    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);
  
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN); 
    gpio_pull_up(BUTTON_A); 
  
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN); 
    gpio_pull_up(BUTTON_B); 
  
    gpio_init(JOYSTICK_PB);
    gpio_set_dir(JOYSTICK_PB, GPIO_IN); 
    gpio_pull_up(JOYSTICK_PB); 

}

void setup_irq(){

    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(JOYSTICK_PB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
  

}

void setup_display(){

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA); // Pull up the data line
    gpio_pull_up(I2C_SCL); // Pull up the clock line
    
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display
  
    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
  
}

int main()
{

  PIO pio = pio0;
  int sm = 0;
  uint offset = pio_add_program(pio, &ws2812_program);

  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

  add_repeating_timer_ms(50, sprinkler_callback, NULL, &timer);//inicializa temporização dos sprinklers
  add_repeating_timer_ms(200, fan_animation_callback, NULL, &timer2);//inicializa temporização da animação dos ventiladores
  add_repeating_timer_ms(500, buzzer_callback, NULL, &timer3);

  stdio_init_all();
  setup_gpio();
  setup_irq();
  setup_display();
  
  
  bool cor = true;

  adc_init();
  adc_gpio_init(JOYSTICK_X_PIN);
  adc_gpio_init(JOYSTICK_Y_PIN); 

  pwm_setup_fan();
  pwm_setup_sprinkler();
  pwm_setup_buzzer();

  uint16_t adc_temp_read;
  uint16_t adc_value_y;  
  int temperature_celsius;
  int resistance;
  char str_x[17];  // Buffer para armazenar a string
  char str_y[17];  // Buffer para armazenar a string  

  while (true)
  {
    
    adc_select_input(0); // Seleciona o ADC para eixo Y. O pino 26 como entrada analógica
    adc_temp_read = adc_read();
    temperature_celsius = adc_temp_read * (( (3.3*1000) / 4096.0) / 10 );
    temperature_celsius /= 6;
    if(temperature_celsius > 40){fan_active = true;}
    else{fan_active = false;}

    adc_select_input(1); // Seleciona o ADC para eixo X. O pino 27 como entrada analógica
    adc_value_y = adc_read(); 
    resistance = (adc_value_y*1012)/4095;  
    if(resistance > 900){buzzer_active = true;}
    else{buzzer_active = false;}

    sprintf(str_x, "Temp atual %.dC", temperature_celsius);  // Converte o inteiro em string
    sprintf(str_y, "resist %d Ohm", resistance);  // Converte o inteiro em string

    pwm_fan_ctrl(temperature_celsius);
    
    ssd1306_fill(&ssd, !cor); // Limpa o display
    ssd1306_draw_string(&ssd, str_x, 3, 23);// exibe o valor de temperatura
    ssd1306_draw_string(&ssd, str_y, 3, 9); // exibe o valor de resistência
    ssd1306_send_data(&ssd); // Atualiza o display

    sleep_ms(1000);
  }
}

void gpio_irq_handler(uint gpio, uint32_t events)
{
   // Obtém o tempo atual em microssegundos
   uint32_t current_time = to_us_since_boot(get_absolute_time());
   
   // Verifica se passou tempo suficiente desde o último evento
   if (current_time - last_time > 400000) // 400 ms de debouncing
   {
       last_time = current_time; // Atualiza o tempo do último evento
       
       

       if(gpio == BUTTON_A)
       {
         sprinkler_active = !sprinkler_active;
         pwm_set_gpio_level(LED_PISCA,0);
         if(!sprinkler_active)
         ssd1306_draw_string(&ssd,"sprink desat",3,40);
         else
         ssd1306_draw_string(&ssd,"sprink ativ",3,40);
       }

       else if (gpio == BUTTON_B)
       {

        ssd1306_fill(&ssd, false);
        ssd1306_send_data(&ssd);
        reset_usb_boot(0, 0);//bootsel 

       }

       else if (gpio == JOYSTICK_PB){
        
       }
       ssd1306_send_data(&ssd);

         
                                                
      
   }



}