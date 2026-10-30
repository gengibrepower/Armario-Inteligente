# Armário Inteligente de Notebooks

Firmware ESP32 e interface web embarcada de um armário que controla retirada e devolução de notebooks por cartão NFC. Trabalho da disciplina Performance em Sistemas Ciberfísicos (PUCPR, 2026/2).

> **Estado atual:** compila e passa nos testes unitários, mas **ainda não foi executado em hardware real**. Use `docs/TESTES.md` para a validação em bancada.

## Visão geral

- **Core 1:** interação física (NFC, máquina de estados, tranca, LEDs, buzzer, OLED).
- **Core 0:** comunicação e armazenamento (Wi-Fi, web, flash, métricas, amostragem, energia).
- **Web sem internet:** todas as páginas e o Chart.js ficam no LittleFS.
- **Persistência:** 1000+ logs, histórico de 24 h e de 7 dias, 1000+ eventos de empréstimo.

Detalhes em [docs/ARQUITETURA.md](docs/ARQUITETURA.md).

## Ligações

| Componente | GPIO | Observações |
|---|---|---|
| PN532 SCK / MISO / MOSI / SS | 18 / 19 / 23 / 4 | Coloque as chaves do módulo em **modo SPI** |
| OLED SDA / SCL | 21 / 22 | I²C, endereço 0x3C |
| Servo (sinal) | 13 | Alimentação 5 V com capacitor de 1000 µF |
| Reed da porta | 25 | Entre GPIO e GND; fechado com a porta fechada |
| Microswitch slot 1 / 2 | 26 / 27 | C no GND, NA no GPIO |
| LED verde / vermelho | 32 / 33 | Resistor de 330 Ω |
| Buzzer | 14 | Base do BC547 por 1 kΩ; buzzer ativo de 5 V no coletor |
| Segundo atuador | 16 | **Pendente**, ver abaixo |

```mermaid
flowchart LR
  ESP[ESP32 DevKit]
  ESP -- SPI --> NFC[PN532]
  ESP -- I2C --> OLED[SSD1306]
  ESP -- PWM 50 Hz --> SERVO[SG90]
  ESP -- GPIO 14 --> Q[BC547] --> BUZ[Buzzer 5 V]
  ESP -- GPIO 32/33 --> LEDS[LEDs]
  ESP -- GPIO 16 --> AUX[Atuador 2]
  REED[Reed] -- GPIO 25 --> ESP
  SW[Microswitches] -- GPIO 26/27 --> ESP
```

Todos os GNDs precisam ser comuns. Não alimente o servo pelo pino de 3,3 V.

## Como compilar e gravar

Requer PlatformIO. Na primeira vez, as dependências são baixadas sozinhas.

```bash
cd ~/Documentos/armario-inteligente
pio run                  # compila o firmware
pio run -t upload        # grava o firmware
pio run -t uploadfs      # grava as páginas web
pio device monitor       # acompanha os logs pela USB
```

**Atenção:** `uploadfs` substitui a partição inteira e **apaga usuários, logs e histórico**. Antes de regravar um armário em uso, baixe o backup em Admin → Sistema. Atualizações de firmware pela aba Performance (OTA) preservam esses dados.

### Testes unitários

```bash
sudo dnf install gcc-c++   # uma vez só; os testes rodam no PC
pio test -e native
```

## Primeiro acesso

1. Ligue o armário. Sem rede configurada, ele cria o Wi-Fi **Armario-Setup**, senha **armario-setup**.
2. Conecte-se e abra `http://192.168.4.1/admin.html`.
3. Entre com usuário **admin** e senha **admin**. O sistema obriga a trocar a senha antes de liberar qualquer função.
4. Em Admin → Configurações, busque a rede, informe a senha e salve. O IP aparece no rodapé do OLED.
5. Em Admin → Usuários, clique em **Cadastrar cartão** e aproxime o cartão.
6. Preencha `data/www/about.json` com nomes, e-mails e link do repositório, e rode `pio run -t uploadfs`.

## Pendências

| Item | Responsável |
|---|---|
| **Confirmar o segundo atuador com a equipe.** O TDE exige dois atuadores que não sejam LED nem buzzer, e só o servo existe. O firmware já aciona um relé (iluminação/cooler) no GPIO 16 enquanto a porta está aberta; `AUX_ACTUATOR_ENABLED` e `AUX_ACTIVE_LEVEL` ficam em `include/config.h`. | Equipe |
| Executar `docs/TESTES.md` e anexar as evidências | Equipe |
| Ajustar ângulos do servo e conferir a polaridade do reed (o KY-025 pode inverter a lógica) | Equipe |
| Preencher nomes, e-mails e GitHub em `data/www/about.json` | Equipe |
| Conferir pilhas das tasks pelo high water mark e reduzir as folgadas | Equipe |

## Limitações conhecidas

- **HTTP sem TLS:** login e cookie trafegam sem criptografia. Use em rede controlada.
- **Dados pessoais:** com "Dashboard e Performance públicas" ligado, quem acessar a rede vê nomes de usuários. Desligue essa opção se o uso exigir, considerando a LGPD.
- **Dormindo, o cartão não acorda o armário:** só a porta e o timer acordam. Durante o sono ninguém consegue retirar notebooks.
- **Modo econômico automático:** reduz a CPU a 80 MHz fora do horário; não liga o modem sleep.
- **Backup sem segredos:** não leva a senha do Wi-Fi nem a do admin.
- **Volume do bipe:** o buzzer é ativo, então o PWM apenas recorta o som; o efeito precisa ser avaliado na bancada.

## Créditos

Chart.js 4.4.9 (licença MIT), Adafruit PN532/SSD1306/GFX (BSD), ArduinoJson (MIT), ESPAsyncWebServer e AsyncTCP (LGPL-3.0).
