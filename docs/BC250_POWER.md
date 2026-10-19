# BC250: start, shutdown, sleep e controllo LAN

## Ambito e comportamento

Questa integrazione aggiunge il controller alla schermata Scarab e al client
Linux esistenti. Target: **Waveshare ESP32-S3-LCD-1.28 con ESP32-S3R2**, display
integrato, 16 MB flash e **2 MB Quad PSRAM**, più la PCB BC250 del progetto KiCad.
Il README storico descrive ancora il vecchio DevKit a quattro display: **non
usare quel cablaggio per questa variante**.

| Origine | Comportamento |
|---|---|
| Tasto breve, ATX noto OFF | Abilita ATX, attende 2 s; impulso POWER BC250 solo se STATE non è già alto |
| Tasto breve, PC acceso | Richiede `systemctl poweroff` tramite l'agente Linux |
| Tasto breve, sleep noto | Impulso breve al POWER BC250 per risvegliare, senza modificare ATX |
| Tasto tenuto almeno 8 s | Forza OFF solo se il latch è noto ON; possibile perdita dati |
| Menu Linux: spegnimento | Conferma finale dal sistema operativo; attende STATE LOW stabile 3 s, poi rilascia ATX |
| Menu Linux: sospensione/ripresa | Hook pre/post: conserva sempre ATX durante sleep, ripristina lo stato al resume |
| Menu Linux: riavvio | Nessuna conferma di OFF, nessun rilascio automatico del latch |
| HTTPS sulla LAN | Start, shutdown pulito, sleep, wake; **nessun forced-off remoto** |
| USB, Wi-Fi o agente assenti | Nessun taglio ATX automatico; richiesta fallita/timeout visibile nello stato |

La pressione breve viene eseguita **al rilascio**, così non viene emesso uno
shutdown prima di riconoscere una pressione lunga. Un tasto tenuto durante il
boot dell'ESP32 è ignorato fino al primo rilascio. I comandi duplicati durante
un'operazione non generano ulteriori impulsi di toggle.

## Cablaggio da usare

| PCB BC250 | Waveshare |
|---|---|
| J3.1 `+5VSB` | H1.18 VSYS, ingresso alimentazione 5 V |
| J3.2 GND | H1.20 GND |
| J3.3 `ESP_BTN_SENSE` | H1.17 GPIO15, ingresso pull-up attivo LOW |
| J3.4 `ESP_POWER_REQ` | H1.15 GPIO16, impulso HIGH 150 ms |
| J3.5 `ESP_STATE` | H1.13 GPIO17, ingresso **massimo 3,3 V** |
| J3.6 `ESP_BC250_BTN_REQ` | H1.11 GPIO18, impulso HIGH 250 ms |

Il progetto indica J4.2 verso **TPMS1 pin 9**, non TPMS1 pin 14 `S_PWRDWN#`.
Il LED 12 V del pulsante rimane cablato esternamente alla PCB, come nel progetto.
Non collegare 5 V ai GPIO. Verificare alimentazioni simultanee VSYS/USB e possibili
ritorni di corrente prima del cablaggio. Questa integrazione non cambia il PCB.

La USB-C Waveshare usa **CH343 → UART0, GPIO43/44, 115200 8N1**. Il firmware ora
supporta questo trasporto; la console UART viene disabilitata per non mescolare
log e protocollo. Per hardware realmente collegato alla USB nativa si può
disabilitare `SCARAB_UART_BRIDGE`, ma non è il cablaggio della Waveshare.

## Prima accensione e reset dell'ESP32

Il latch 74HC74 si comanda a **toggle**: non esiste un GPIO di lettura del latch.
STATE LOW può significare PC spento, in sleep o non ancora avviato. Anche STATE
HIGH non dimostra che Linux sia sveglio. Per questo il firmware parte in
`unknown`, **senza impulsi**:

- Se Linux è già attivo, l'agente sincronizza automaticamente il controller.
- Se l'ATX è realmente spento, misurarlo/verificarlo e usare la sezione
  **Risincronizzazione iniziale → Conferma ATX già spento** della pagina HTTPS.
  Questo non cambia il latch: dichiara un fatto verificato dall'operatore.
- In alternativa, da una seriale locale fidata inviare `PWR_CONFIRM:OFF` e leggere
  `PWR_GET`. Non farlo dal normale agente in esecuzione sulla BC250 accesa.
- Se l'ESP32 si resetta mentre la BC250 dorme, il solo hardware attuale non
  consente sempre di recuperare lo stato: **non confermare OFF**. Risvegliare la
  BC250 con un mezzo indipendente e attendere la risincronizzazione Linux.
  Per eliminare questo limite serve un feedback hardware aggiuntivo del latch.

Se il segnale di stato è alto, il firmware può riconoscere un latch ON ma rimane
prudente sullo stato del sistema operativo. Una pressione lunga può comunque
forzare OFF quando ON è noto. Nessun valore persistito viene usato come prova
del latch dopo un reset.

## Firmware e Wi-Fi di casa

Prerequisito: **ESP-IDF 5.4.3**, toolchain ESP32-S3 e OpenSSL. Usare un checkout
pulito oppure rieseguire menuconfig: vecchi `sdkconfig` non vengono sovrascritti
dai nuovi defaults, in particolare per PSRAM e console.

1. Riservare nel router un IPv4 per l'ESP32 e generare materiale locale:

   ```sh
   python3 tools/generate_bc250_local.py --ip 192.168.1.50
   ```

   L'indirizzo è un **esempio**, da sostituire. Il tool crea certificato, chiave
   privata e token casuale in `local/`, già ignorata da Git; rifiuta sovrascritture.
   Non stampa il token. Non inviare password Wi-Fi o token nelle chat/issue.

2. In `idf.py menuconfig` → **BC250 power controller**:
   - abilitare `BC250_WIFI`;
   - inserire SSID e password della rete **2,4 GHz WPA2/WPA3 compatibile WPA2**;
   - copiare localmente il contenuto di `local/api-token` in `BC250_API_TOKEN`;
   - mantenere `local/tls` come directory TLS;
   - verificare GPIO15/16/17/18, soglia lunga e ritardo iniziale.

3. Controllare Quad PSRAM e console `None`, poi:

   ```sh
   idf.py -DIDF_TARGET=esp32s3 build
   idf.py -p /dev/serial/by-id/IL_TUO_CH343 flash
   ```

Wi-Fi è disabilitato per default finché non viene configurato. Credenziali o TLS
non validi non attivano una modalità HTTP aperta. Nessun AP di provisioning,
servizio cloud o port forwarding. Il server ascolta HTTPS/443 sull'IP assegnato;
il router e la riga seriale `PWR_NET:https://.../` permettono di trovarlo.
`bc250-controller` è un hostname DHCP, **non un servizio mDNS `.local`**.

Installare/verificare il certificato generato sul dispositivo dal quale si
accede; non ignorare alla cieca gli avvisi TLS. Per un controllo da terminale:

```sh
curl --cacert local/tls/servercert.pem \
  -H "Authorization: Bearer $(cat local/api-token)" \
  https://192.168.1.50/api/status
```

Il token immesso nella pagina resta in memoria, non in cookie/localStorage/URL.
Le operazioni richiedono POST senza body e `Authorization: Bearer ...`.
`/api/start`, `/api/stop`, `/api/sleep`, `/api/wake` rispondono **202 accodato**:
verificare poi `/api/status`, soprattutto `state` e `last_error`. Una richiesta
incompatibile con lo stato viene rifiutata dal controller, senza impulsi.
`atx_expected_on` è una stima logica, **non una misura delle rail**.
`/api/confirm-off` è riservato alla conferma fisica descritta sopra.

Durante `suspending` non vengono generati impulsi di risveglio: Linux potrebbe
essere ancora attivo. Il risveglio hardware richiede l'hook pre-suspend ricevuto
e STATE LOW stabile; se STATE rimane HIGH anche in sleep, il firmware non può
certificare questo percorso con i dati attuali e rifiuta l'impulso. Occorre prima
misurare/integrare un segnale di sleep affidabile o usare un mezzo di risveglio
indipendente. STATE che torna HIGH blocca nuovi impulsi prima del resume Linux.

**Non distribuire `sdkconfig`, chiavi, token o binari costruiti con le tue
credenziali.** L'immagine firmware contiene i segreti di questa configurazione;
flash encryption/secure boot non sono configurati da questa integrazione.

## Agente Linux e hook systemd

Il backend supportato è Linux con **systemd** e `systemctl`. Non viene configurato
automaticamente il computer sul quale stai leggendo questo documento.
Verificare prima che la BC250 supporti davvero suspend/resume con il suo
BIOS/kernel: il software non può aggiungere una modalità ACPI assente.

Installazione proposta, da adattare sulla **BC250 reale**:

1. Collocare una copia **root-owned, non modificabile da utenti non privilegiati**
   del progetto in `/opt/pc-monitor`. Creare lì `.venv` e installare
   `linux_client/requirements.txt`. L'agente riusa `HardwareMonitor` e le
   impostazioni display del client esistente; non eseguire contemporaneamente
   il vecchio client sulla stessa seriale.
2. Scrivere `/etc/bc250-power.conf`, root-owned e mode 0600:

   ```ini
   BC250_SERIAL=/dev/serial/by-id/IL_TUO_CH343
   ```

   Scrivere lo **stesso percorso, da solo e senza `BC250_SERIAL=`**, nel file
   `/etc/bc250-power-device`, root-owned e mode 0600.
3. Installare `linux_client/systemd/bc250-monitor.service` in
   `/etc/systemd/system/`. Installare `bc250-sleep` come eseguibile mode 0755 in
   `/usr/lib/systemd/system-sleep/` (adattare il percorso se la distro usa altro).
4. Compilare il notifier **sulla BC250**, quindi installarlo come eseguibile in
   `/usr/lib/systemd/system-shutdown/bc250-final`:

   ```sh
   cc -std=c11 -O2 -Wall -Wextra -Werror \
     linux_client/systemd/bc250_final_shutdown.c -o /tmp/bc250-final
   ```

   **Non** usare `-DBC250_HOOK_TEST` nell'installazione reale. Il notifier non
   richiede Python o l'agente al termine dello shutdown. Si esegue solo per
   `poweroff`/`halt`, mai per `reboot`/`kexec`; invia la conferma finale tre volte.
   In mancanza della conferma il controller conserva ATX ON.
5. Solo dopo aver verificato configurazione e cablaggio, usare `systemctl
   daemon-reload` e abilitare `bc250-monitor.service`. La configurazione del
   servizio esegue azioni di alimentazione come root: limitare scrupolosamente
   i permessi di tutti i file `/opt/pc-monitor` e della sua virtualenv.

Avvio manuale alternativo:

```sh
python3 linux_client/pc_monitor.py --power-control --port /dev/serial/by-id/IL_TUO_CH343
```

Per il solo bridge, senza dipendenze dei sensori:

```sh
python3 linux_client/power_agent.py --port /dev/serial/by-id/IL_TUO_CH343 --no-telemetry --dry-run
```

`--dry-run` rifiuta ed annota le richieste OS: **non simula né disabilita i GPIO
del firmware**. I test automatici non aprono porte reali e non eseguono shutdown
o suspend. In produzione non usare due processi contemporanei sulla seriale.
Il bridge evita di asserire DTR/RTS all'apertura, ma eventuali glitch/reset del
CH343 vanno comunque verificati sull'hardware.

## Limiti hardware da verificare prima dell'uso

Non risultano provati nei dati del progetto:

- Livelli reali di TPMS1.9 in RUN, sleep e OFF; alimentazioni residue durante
  shutdown e corretto arrivo del segnale LOW. Se STATE resta HIGH, **ATX non si
  spegne automaticamente**, per sicurezza.
- Supporto di sospensione e wake da POWER del BIOS/kernel BC250.
- Se la BC250 fa auto-boot all'arrivo delle rail; valori corretti per il ritardo
  iniziale di 2 s e gli impulsi 150/250 ms. Sono default configurabili o costanti
  esplicite, **non tempi misurati sui componenti reali**.
- Disponibilità USB/alimentazione standby durante sleep/shutdown e comportamento
  DTR/RTS del CH343. Una conferma finale persa lascia ATX ON, mai un taglio forzato.
- Reset dell'ESP32 mentre il PC dorme, senza feedback del latch: limite spiegato
  nella sezione di risincronizzazione, non risolvibile con il solo firmware.

Non abilitare la sospensione o provare lo spegnimento forzato su dati importanti
prima di questi collaudi. Pressione lunga = possibile perdita dati deliberata;
nessun timeout di shutdown viene trasformato automaticamente in forced-off.

## Verifica software

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain/power \
  tests/test_power_fsm.c main/power/power_fsm.c -o /tmp/bc250-tests
/tmp/bc250-tests
python3 -m unittest discover -s tests -v
```

La CI aggiunta compila inoltre il firmware con ESP-IDF 5.4.3, sia con rete
disabilitata sia con HTTPS e credenziali sintetiche. La presenza del workflow
**non significa che una run GitHub sia già stata eseguita**. I test host non
sostituiscono il build ESP-IDF e il collaudo sulla scheda.
