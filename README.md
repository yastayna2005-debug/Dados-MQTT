# Projeto MQTT com Raspberry Pi Pico W

## Aluna

**Nome:** Yasmim Tayna
**RA:** 240025519

---

## Descrição

Este projeto implementa comunicação MQTT utilizando a placa **Raspberry Pi Pico W**. O sistema conecta-se a uma rede Wi-Fi, realiza comunicação com um broker MQTT e permite a troca de informações por meio de tópicos específicos.

Quando o botão da placa é pressionado, a temperatura interna do microcontrolador é lida e publicada em um tópico MQTT. Além disso, o sistema recebe comandos por assinatura MQTT para controlar o LED integrado da placa.

---

## Funcionalidades

* Conexão Wi-Fi utilizando Raspberry Pi Pico W.
* Conexão com broker MQTT.
* Publicação da temperatura interna do microcontrolador.
* Assinatura de tópico MQTT para controle do LED integrado.
* Tratamento de mensagens recebidas.
* Controle do LED por meio de comandos enviados via MQTT.

---

## Configuração da Rede

```c
#define WIFI_SSID  "ALUNO"
#define WIFI_SENHA "aluno123"
```

---

## Broker MQTT

```c
#define MQTT_BROKER_IP "34.243.217.54"
```

Porta utilizada:

```text
1883
```

---

## Tópicos MQTT

### Publicação de Temperatura

Tópico utilizado para enviar a temperatura interna da Raspberry Pi Pico W:

```text
/yasmim_tayna/240025519/temperatura
```

### Assinatura para Controle do LED

Tópico utilizado para receber comandos de controle do LED:

```text
/yasmim_tayna/240025519/led
```

---

## Funcionamento

### Envio de Temperatura

Ao pressionar o botão da placa:

1. O sensor de temperatura interno é lido.
2. A temperatura é convertida para graus Celsius.
3. O valor é publicado no tópico MQTT de temperatura.

Exemplo de mensagem enviada:

```text
26.54 °C
```

### Controle do LED

O firmware recebe valores inteiros pelo tópico:

```text
/yasmim_tayna/240025519/led
```

Comportamento:

| Valor recebido   | Ação                                           |
| ---------------- | ---------------------------------------------- |
| 0                | LED desligado                                  |
| Inteiro positivo | LED pisca no intervalo informado (em segundos) |
| Valor inválido   | LED desligado                                  |

Exemplos:

```text
0
```

Desliga o LED.

```text
2
```

Faz o LED piscar a cada 2 segundos.

```text
5
```

Faz o LED piscar a cada 5 segundos.

---

## Hardware Utilizado

* Raspberry Pi Pico W
* Sensor de temperatura interno
* LED integrado da placa
* Botão integrado da placa

---

## Bibliotecas Utilizadas

* pico/stdlib.h
* pico/cyw43_arch.h
* hardware/adc.h
* lwip/apps/mqtt.h
* bsp/board.h

---

## Estrutura Geral do Programa

O programa realiza as seguintes etapas:

1. Inicialização do sistema e periféricos.
2. Conexão à rede Wi-Fi.
3. Conexão ao broker MQTT.
4. Assinatura do tópico de controle do LED.
5. Monitoramento do botão da placa.
6. Publicação da temperatura quando o botão é pressionado.
7. Recebimento de comandos MQTT para controle do LED.

---

## Resultado Esperado

* Publicação da temperatura interna da Raspberry Pi Pico W no broker MQTT.
* Controle remoto do LED integrado por meio de mensagens MQTT.
* Comunicação bidirecional entre dispositivo IoT e broker MQTT.
