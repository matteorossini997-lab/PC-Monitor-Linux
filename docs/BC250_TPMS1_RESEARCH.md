# TPMS1 BC250: letture e limiti documentati online

Ricerca consultata il **19 ottobre 2026**. Sono fonti community e progetti di
reverse engineering: non un datasheet elettrico ufficiale AMD/ASRock, né misure
eseguite sulla scheda dell'utente. Nessuna modifica al PCB deriva da questa ricerca.

## Risultati

| Punto | Evidenza pubblica | Limite |
|---|---|---|
| TPMS1.9, `3V` | Documentazione community e controller esterni lo descrivono a circa **3,3 V da acceso**, **0 V da spento/dopo shutdown software** | Non è una tabella di tutti gli stati ACPI o una specifica di soglie/transitori |
| Livello caricato | Il codice Thunkar segnala circa **2,9 V** e instabilità di `digitalRead()` nel proprio circuito ESP32-C3 | Osservazione di quel circuito, non tensione garantita della BC250 o di `ESP_STATE` sulla nostra PCB |
| TPMS1.15, `3VSB` | Rail distinta da `3V` nel pinout originale | Non usarla come equivalente dello stato RUN; non confonderla con il `+5VSB` ATX |
| TPMS1.14, `S_PWRDWN#` | Nome/pin presenti nel pinout originale | Non ho trovato una tabella verificata delle sue tensioni in RUN, suspend e shutdown: non propongo di sostituirlo a TPMS1.9 |
| Sleep nativo | SkillFishOS descrive s2idle senza risveglio; Lethe descrive suspend che blocca la BC250 | Non è corretto presentare il solo `systemctl suspend` come soluzione già funzionante sulla BC250 |
| Pseudo-sleep Lethe | Pausa processi, audio e display; scheda e sistema operativo rimangono alimentati | Non è S3/s2idle né OFF; le fonti non riportano una misura esplicita di TPMS1.9 durante questa pausa |

## Fonti e cosa dimostrano

1. **Pinout originale reverse engineered**:
   [mothenjoyer69/bc250-documentation, hardware.md](https://github.com/mothenjoyer69/bc250-documentation/blob/1cf461b7766d43b22753f136020b304a90d80c61/hardware.md#tpms1).
   Conferma `3V` al pin 9, `3VSB` al 15, `S_PWRDWN#` al 14 e GND al
   2/12/17/18. Da solo **non** fornisce misure in tutti gli stati del sistema.

2. **Guida community**:
   [elektricM, TPMS1 3.3V Power Rail](https://elektricm.github.io/amd-bc250-docs/hardware/pinouts/#tpms1-33v-power-rail).
   Descrive il pin 9 come attivo solo con scheda accesa, adatto al controllo di
   alimentazione. La pagina attribuisce parte delle informazioni al punto 1:
   non conto le due pagine come due campagne di misura indipendenti.

3. **Controller con osservazioni sul livello letto**:
   [Thunkar, include/board.h](https://github.com/Thunkar/bc250-esp32-switch/blob/2af17649c469c628a6ae1d04dc924f9141da8d0a/include/board.h)
   e [README](https://github.com/Thunkar/bc250-esp32-switch#readme).
   Il codice commenta livello vicino a 2,9 V e lettura digitale instabile;
   implementa ADC con soglie HIGH 2000 mV, LOW 800 mV e media di 16 letture.
   **Questi sono parametri del suo firmware, non tensioni misurate da copiare
   come specifica**, e il suo ESP32-C3 non è il nostro ESP32-S3 con PCB intermedia.

4. **Altro controller che utilizza il pin 9**:
   [GreatApo, PC-state monitor](https://github.com/GreatApo/BC250_ESP32_ATX_PSU#pc-state-monitor).
   Associa TPMS1.9 all'ingresso di rilevazione PC acceso e mantiene PS_ON durante
   lo shutdown fino al monitor LOW. Non documenta un ciclo di suspend/resume.

5. **Lethe, alimentazione e collaudo**:
   [Soft power control](https://lethevimlet.github.io/lethe-bc250/power.html)
   e [Wiring, passi 6 e 8](https://lethevimlet.github.io/lethe-bc250/power-wiring.html).
   Descrive 3,3 V che scompaiono allo shutdown, ma esplicita che la premessa viene
   dalla community e va verificata sulla propria scheda. Avverte inoltre che
   i pin LPC adiacenti possono essere alti anch'essi: una misura alta da sola
   **non identifica il pin corretto**. Non adottare il suo cablaggio al posto
   della PCB BC250 già progettata.

6. **Limiti della sospensione**:
   [SkillFishOS, Broken ACPI suspend](https://www.skillfishos.com/en/docs/hardware-bc250#broken-acpi-suspend)
   e [Lethe, Fake sleep](https://lethevimlet.github.io/lethe-bc250/sleep.html).
   Entrambi segnalano che il suspend reale può richiedere reset/taglio di
   alimentazione per recuperare. Lethe usa invece una pausa software con scheda
   alimentata, non un risveglio hardware da vero suspend. Le sue funzioni di
   pausa richiedono integrazione specifica Steam/Decky/gamescope.

Un post Reddit su un circuito a relè è emerso nella ricerca, ma l'accesso diretto
ha restituito HTTP 403: non tratto il solo snippet del motore come una misura
verificata. Le conclusioni sopra hanno fonti direttamente leggibili.

## Conseguenze per il nostro progetto

- **Acceso/spento:** la ricerca supporta la scelta originale di TPMS1.9, non la
  sostituzione con pin 14 o 15. Rimangono utili conferma OS e filtro temporale.
- **Lettura:** non cambio alla cieca GPIO17 in ADC o copio soglie da un C3.
  `ESP_STATE` attraversa il circuito BC250: il livello effettivo sul GPIO va
  distinto dalla tensione letta direttamente su TPMS1.9.
- **Sleep:** manca una caratterizzazione elettrica affidabile RUN/SLEEP/OFF di
  TPMS1.9, e soprattutto c'è evidenza di suspend nativo problematico. Il backend
  `systemctl suspend` presente nel software è quindi **non validato e da non
  attivare senza verifiche**. Questa ricerca non implementa né abilita uno
  pseudo-sleep o un nuovo backend al posto suo.
- Un futuro pseudo-sleep andrebbe concordato in base alla distro/desktop e
  notificato esplicitamente dall'OS; non dovrebbe aspettarsi STATE LOW né
  risvegliare una scheda ancora attiva con un impulso POWER.

La soglia richiesta per il pulsante fisico è **5 secondi** di pressione
stabilizzata, non 8. Firmware, menuconfig, documentazione e test del limite
4999/5000 ms vengono aggiornati insieme.

## Riferimenti aggiunti dall'utente: mosfet.party e Reddit

La [pagina LITE/DIY](https://mosfet.party/products/adapter-2) e il
[PDF di cablaggio del produttore](https://github.com/mosfetparty/bc250-psu-adapter/blob/main/Universal%20%E2%80%94%20Lite%20Edition/Wiring%20Diagram/Lite%20-%20BC250%20PSU%20Adapter.pdf)
sono stati aperti e verificati anche nelle immagini, non solo nel testo.

Il diagramma identifica un'uscita opzionale `BTN_OUT` verso un pad del gruppo
POWER/RESET **sul lato inferiore della BC250**. Il pad è cerchiato in rosso nel
riquadro in alto a sinistra: è un riferimento visivo utile per `J4.1`, senza
inventare un pin POWER dentro TPMS1. Prima della saldatura controllare che il
layout della propria scheda corrisponda e identificare il contatto POWER, non RESET.

| Segnale adattatore mosfet.party | Funzione corrispondente nel nostro progetto |
|---|---|
| `BTN_OUT` | J4.1 `BC250_PWR_BTN`, verso il contatto segnale POWER |
| `HOST_ON` da TPMS1.9 | J4.2 `ESP_STATE` |
| `GND` | Massa comune; J4.3 verso GND BC250, per esempio TPMS1.17 |
| `PS_ON` | J1_ATX1.3, verso PS_ON# ATX |
| `5V_STB` | Ingresso standby ATX J1_ATX1.1, prima del nostro fusibile |
| Coppia `BTN+` / `BTN-` | Funzione del nostro J2, contatto del pulsante esterno |

Questa è una corrispondenza **funzionale**, non un'indicazione di collegare i due
controller in parallelo o di sostituirne i componenti alla cieca. Il produttore
non pubblica in quel PDF lo schema elettrico interno completo né una prova di
suspend/resume: non ne deduco tempi, tensioni del pad POWER o supporto allo sleep.

**Differenza importante:** il suo diagramma permette, in alternativa a TPMS1,
di prelevare `HOST_ON` dal secondo pin di CPU_FAN1. Nel pinout BC250 quel punto è
la rail **12 V** della ventola: non copiarlo sul nostro J4.2/ESP_STATE, che arriva
all'ingresso ESP32 da 3,3 V. Il produttore vieta inoltre di collegare le due fonti
HOST_ON contemporaneamente. Per noi resta TPMS1.9, non CPU_FAN1.

Anche la sua uscita LED 5 V/15 mA è specifica del suo circuito: non cambia il
cablaggio esterno a 12 V previsto per il pulsante acquistato dall'utente.

**Pulsante posteriore con ATX OFF:** nella nostra PCB J4.1 è l'uscita del
fototransistor U5, non un ingresso di lettura del pulsante posteriore. Il
pulsante osservato dall'ESP32 è quello esterno su J2. Quindi non prometto che
premere il pulsante posteriore riaccenda un ATX già disabilitato dal nostro
latch: per quella funzione occorre un percorso aggiuntivo di rilevazione o un
diverso circuito standby, non il solo cambio di firmware.

I due link Reddit forniti dall'utente (`1so14qn` e `1ueu79e`) non sono risultati
leggibili direttamente: pagine vuote/interstiziali e richieste JSON HTTP 403.
Non attribuisco ai loro commenti risultati di lettura o di sleep non verificati;
servono testo o screenshot per integrarli come evidenza diretta.
