#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "lwip/apps/mqtt.h"
#include "bsp/board.h"



// Definições de rede e identificação do servidor
#define MQTT_BROKER_IP "34.243.217.54"

// Parametros de acesso ao Wi-Fi local
#define WIFI_SSID   "ALUNO"
#define WIFI_SENHA "aluno123"
#define WIFI_AUTH  CYW43_AUTH_WPA2_AES_PSK
    
// Identificação do aluno utilizada na composição dos tópicos MQTT 
#define NOME "yasmim_tayna"
#define RA   "240025519"

// Montagem automática dos tópicos de transmissão e recebimento
#define TOPICO_TEMP_PUB "/" NOME "/" RA "/temperatura"
#define TOPICO_LED_SUB  "/" NOME "/" RA "/led"

// Mapeamentos de hardware e buffers de dados
#define CANAL_ADC_INTERNO 4
#define BUFFER_MSG_SIZE 64
#define BUFFER_TOPIC_SIZE 128

// Estrutura padrão exigida pelo lwIP com dados básicos de identificação MQTT
struct mqtt_connect_client_info_t mqtt_user_info =
{
    "pico_w_aluno", // Identificador do cliente
    NULL, NULL, 0, NULL, NULL, 0, 0
};

// Variáveis globais para gerenciar o estado da conexão e os buffers de mensagens
static mqtt_client_t *canal_mqtt = NULL;
static bool mqtt_is_connected = false;

// Controle de temporização para fazer o LED piscar de forma assíncrona
static volatile uint32_t led_periodo_ms = 0;
static absolute_time_t led_schedule_time;
static bool status_led_atual = false;

// Alocação dos buffers para processar strings que chegam via rede
static char msg_buffer[BUFFER_MSG_SIZE + 1];
static uint16_t msg_index = 0;
static char current_topic_name[BUFFER_TOPIC_SIZE];

// Realiza a conversão de leitura analógica do sensor de temperatura da própria CPU
static float obter_temperatura_interna(void) {
    adc_select_input(CANAL_ADC_INTERNO); // Seleciona o canal do sensor interno
    uint16_t raw_adc = adc_read();       // Faz a leitura digital crua (0 a 4095)
    
    // Converte o valor digital obtido de volta para tensão elétrica (0V a 3.3V)
    float voltagem = raw_adc * (3.3f / 4096.0f);
    
    // Fórmula matemática padrão do fabricante para converter tensão para graus Celsius
    return 27.0f - (voltagem - 0.706f) / 0.001721f;
}

// Controla o LED com base nos comandos em segundos recebidos via rede
static void gerenciar_estado_led(uint32_t intervalo_segundos) {
    // Se o valor recebido for zero, desliga o LED completamente
    if (intervalo_segundos == 0) {
        led_periodo_ms = 0;
        status_led_atual = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        printf("[LED] Indicador desativado.\n");
        return;
    }

    // Caso contrário, calcula o tempo em milissegundos e ativa o piscar automático
    led_periodo_ms = intervalo_segundos * 1000u;
    status_led_atual = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    led_schedule_time = make_timeout_time_ms(led_periodo_ms); // Agenda o próximo piscar
    printf("[LED] Modo intermitente ativo: %u s\n", (unsigned)intervalo_segundos);
}

// Limpa espaços extras ou quebras de linha invisíveis ao redor da mensagem recebida
static void limpar_caracteres_escape(char *str, uint16_t *len) {
    if (!str || !len) return;
    
    // Varre e remove caracteres do final da string
    while (*len > 0) {
        char ultimo = str[*len - 1];
        if (ultimo == '\r' || ultimo == '\n' || ultimo == ' ' || ultimo == '\t') {
            str[--(*len)] = '\0';
        } else {
            break;
        }
    }
    
    // Varre e descarta espaços ou caracteres em branco do início da string
    uint16_t idx_inicio = 0;
    while (idx_inicio < *len) {
        char primeiro = str[idx_inicio];
        if (primeiro == ' ' || primeiro == '\t' || primeiro == '\r' || primeiro == '\n') {
            idx_inicio++;
        } else {
            break;
        }
    }
    
    // Ajusta o array de texto na memória caso tenha removido espaços iniciais
    if (idx_inicio > 0) {
        uint16_t tamanho_final = *len - idx_inicio;
        memmove(str, str + idx_inicio, tamanho_final);
        str[tamanho_final] = '\0';
        *len = tamanho_final;
    }
}

// Tenta converter com segurança o texto recebido em um número inteiro limpo
static bool extrair_numero_inteiro(const char *str, uint32_t *resultado) {
    if (!str || *str == '\0') return false;
    
    uint32_t acumulador = 0;
    for (const char *curr = str; *curr != '\0'; ++curr) {
        // Retorna falso caso encontre qualquer caractere que não seja um número
        if (!isdigit((unsigned char)*curr)) return false;
        
        uint32_t digito = (uint32_t)(*curr - '0');
        // Proteção contra estouro de memória de inteiros (Overflow)
        if (acumulador > (UINT32_MAX - digito) / 10u) return false;
        
        acumulador = acumulador * 10u + digito;
    }
    *resultado = acumulador;
    return true;
}

// Função de resposta (callback) acionada quando os dados de um pacote MQTT chegam
static void callback_dados_mqtt(void *arg, const uint8_t *data, uint16_t len, uint8_t flags) {
    // Copia os dados recebidos para o nosso buffer interno temporário
    for (uint16_t idx = 0; idx < len; ++idx) {
        if (msg_index < BUFFER_MSG_SIZE) {
            msg_buffer[msg_index++] = (char)data[idx];
        }
    }

    // Verifica se este pedaço de dado é a parte final da mensagem atual
    if (flags & MQTT_DATA_FLAG_LAST) {
        msg_buffer[msg_index] = '\0'; // Finaliza a string
        limpar_caracteres_escape(msg_buffer, &msg_index); // Limpa sujeiras de formatação

        printf("[MQTT Rx] Topico: %s | Conteudo: %s\n", current_topic_name, msg_buffer);

        // Se a mensagem chegou através do tópico de controle do LED
        if (strcmp(current_topic_name, TOPICO_LED_SUB) == 0) {
            uint32_t valor_detectado = 0;
            bool conversao_ok = extrair_numero_inteiro(msg_buffer, &valor_detectado);

            if (conversao_ok) {
                printf("[Acao] Inteiro processado (%u). Atualizando periferico.\n", (unsigned)valor_detectado);
                gerenciar_estado_led(valor_detectado);
            } else {
                printf("[Erro] Conteudo nao numerico ('%s'). Desligando LED.\n", msg_buffer);
                gerenciar_estado_led(0); // Em caso de mensagem inválida, desliga o LED por segurança
            }
        }
        msg_index = 0; // Reseta o ponteiro do buffer para a próxima mensagem
    }
}

// Callback acionado pelo lwIP para nos informar qual tópico está transmitindo no momento
static void callback_topico_mqtt(void *arg, const char *topic, uint32_t len) {
    printf("[MQTT] Capturando payload para o topico: %s\n", topic);
    strncpy(current_topic_name, topic, sizeof(current_topic_name) - 1);
    current_topic_name[sizeof(current_topic_name) - 1] = '\0';
    msg_index = 0; // Prepara o buffer de dados para receber o conteúdo real
}

// Retorno geral para monitorar se envios ou inscrições deram certo na rede
static void callback_operacao_status(void *arg, err_t err) {
    printf("[Status] Finalizacao da operacao com retorno: %d\n", err);
}

// Callback invocado sempre que o status de conexão com o Broker MQTT é alterado
static void callback_conexao_status(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    printf("[Broker] Resposta de autenticacao: %d\n", status);
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("[Broker] Conectado e autenticado com sucesso.\n");
        mqtt_is_connected = true;

        // Estando conectado, se inscreve de forma automática no tópico de recepção do LED
        err_t sub_err = mqtt_subscribe(client, TOPICO_LED_SUB, 0, callback_operacao_status, NULL);
        if (sub_err != ERR_OK) {
            printf("[Falha] Incapaz de assinar o topico %s (Cod: %d)\n", TOPICO_LED_SUB, sub_err);
        } else {
            printf("[Sucesso] Monitorando o topico %s\n", TOPICO_LED_SUB);
        }
    } else {
        mqtt_is_connected = false; // Desativa a flag caso a conexão tenha caído ou falhado
    }
}

// Coleta a temperatura atual e empacota para publicar via MQTT
static void transmitir_leitura_temperatura(void) {
    if (!mqtt_is_connected || canal_mqtt == NULL) {
        printf("[Aviso] Transmissao abortada: sem sinal MQTT.\n");
        return;
    }

    float temp_atual = obter_temperatura_interna();
    char str_payload[32];
    // Formata o valor numérico em string adicionando a unidade (°C)
    int format_len = snprintf(str_payload, sizeof(str_payload), "%.2f \xC2\xB0""C", temp_atual);
    if (format_len <= 0) return;

    printf("[TX] Enviando para '%s' -> [%s]\n", TOPICO_TEMP_PUB, str_payload);

    // Envia o payload via MQTT de forma assíncrona para a rede
    err_t pub_err = mqtt_publish(canal_mqtt,
                                 TOPICO_TEMP_PUB,
                                 str_payload,
                                 (u16_t)format_len,
                                 0, // QoS 0
                                 0, // Retain desativado
                                 callback_operacao_status,
                                 NULL);
    if (pub_err != ERR_OK) {
        printf("[Falha] Falha ao injetar dados via publish (Cod: %d)\n", pub_err);
    }
}

int main() {
    stdio_init_all();
    board_init(); // Configura periféricos básicos da placa física

    // Configura o conversor analógico e ativa o canal específico do termistor interno
    adc_init();
    adc_set_temp_sensor_enabled(true);

    sleep_ms(5000); // Delay técnico inicial para estabilização do monitor serial

    printf("========================================\n");
    printf("   ESTACAO IOT - RASPBERRY PI PICO W    \n");
    printf("   Discente: %s\n", NOME);
    printf("   Registro Academico: %s\n", RA);
    printf("   Publish TX:  %s\n", TOPICO_TEMP_PUB);
    printf("   Subscribe RX: %s\n", TOPICO_LED_SUB);
    printf("   Endereço Host: %s:1883\n", MQTT_BROKER_IP);
    printf("========================================\n");

    // Inicializa o chip físico responsável pela conectividade sem fio
    if (cyw43_arch_init()) {
        printf("[Fatal] Erro na inicializacao do chip CYW43.\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode(); // Ativa modo de estação (para conectar a um roteador)
    printf("[Wi-Fi] Varrendo rede: %s...\n", WIFI_SSID);

    // Tenta autenticar na rede Wi-Fi usando os parâmetros de segurança fornecidos
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_SENHA, WIFI_AUTH, 30000)) {
        printf("[Wi-Fi] Limite de tempo esgotado ao tentar autenticar.\n");
        return -1;
    } else {
        uint8_t *local_ip = (uint8_t*) &(cyw43_state.netif[0].ip_addr.addr);
        printf("[Wi-Fi] Associado! IP Atribuido: %d.%d.%d.%d\n", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    }

    // Configura e converte a string de IP do Broker para o formato binário de rede
    ip_addr_t broker_addr;
    ip4addr_aton(MQTT_BROKER_IP, &broker_addr);

    // Instancia o cliente MQTT na memória de forma dinâmica
    canal_mqtt = mqtt_client_new();
    if (canal_mqtt == NULL) {
        printf("[Erro] Alocacao de memoria para o cliente MQTT falhou.\n");
        return -1;
    }

    // Vincula os callbacks que vão gerenciar o recebimento das mensagens recebidas por assinatura
    mqtt_set_inpub_callback(canal_mqtt, &callback_topico_mqtt, &callback_dados_mqtt, NULL);

    // Solicita o início da conexão com o servidor na porta padrão 1883
    err_t connection_request = mqtt_client_connect(canal_mqtt, &broker_addr, 1883, &callback_conexao_status, NULL, &mqtt_user_info);
    if (connection_request != ERR_OK) {
        printf("[Erro] Handshake inicial com o servidor recusado.\n");
        return 1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // Inicia com o LED apagado

    bool estado_anterior_botao = false;
    
    // Loop de execução contínuo do sistema embarcado
    while (true) {
        bool estado_atual_botao = board_button_read(); // Lê o botão integrado na PCB
        
        // Mecanismo simples de detecção de clique (Borda de subida com trava)
        if (estado_atual_botao && !estado_anterior_botao) {
            transmitir_leitura_temperatura(); // Dispara o envio ao apertar o botão
            sleep_ms(50);
        }
        estado_anterior_botao = estado_atual_botao;

        // Gerenciador de tarefas temporizadas do LED (Sem usar funções de delay travantes)
        if (led_periodo_ms > 0) {
            if (absolute_time_diff_us(get_absolute_time(), led_schedule_time) <= 0) {
                status_led_atual = !status_led_atual; // Inverte o estado lógico (Toggle)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status_led_atual);
                led_schedule_time = make_timeout_time_ms(led_periodo_ms); // reagenda o tempo do próximo ciclo
            }
        }

        sleep_ms(50); // Alívio dinâmico para reduzir consumo de processamento da CPU
    }
}
