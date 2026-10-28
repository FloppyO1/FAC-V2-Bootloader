# Bootloader USB CDC della F.A.C. V2

Bootloader per la **F.A.C. V2** (Floppy Ant Controller, STM32F072CBT6). Permette di aggiornare il firmware dal
FacTool via USB **senza driver**: la scheda resta una porta seriale CDC. Un aggiornamento interrotto lascia la scheda
nel bootloader, pronta a ripetere l'installazione. Il bootloader **non si aggiorna da sé**: si installa solo con la
DFU ROM di ST (pulsante BOOT), dal recupero DFU del FacTool o con STM32CubeProgrammer.

La specifica completa (mappa della memoria, strutture, protocollo, avvio) è `docs/firmware-specs/bootloader.md` del
FacTool; le modifiche richieste al firmware applicativo sono in `firmware-bootloader.md`, accanto.

## In breve

| Area | Indirizzi | Contenuto |
|---|---|---|
| Flash, pagine 0–7 | `0x08000000`–`0x08003FFF` | bootloader; blocco informativo `"FACB"` a `0x080000C0` |
| Flash, pagine 8–62 | `0x08004000`–`0x0801F7FF` | applicazione; intestazione `"FACA"` a `0x080040C0` |
| Flash, pagina 63 | `0x0801F800` | record «app valida» `"FACR"` |
| RAM | `0x20000000`–`0x200000FF` | vettori dell'app copiati in SRAM + blocco condiviso `"FACS"` a `0x200000C0` |

- **A ogni reset** il bootloader controlla l'app (intestazione, vettori, record, CRC-32) a 8 MHz, prima di qualsiasi
  inizializzazione, e la avvia. Resta attivo se l'app manca o non è valida, se l'app lo chiede (comando `8` dal
  FacTool) o se l'app è ripartita 3 volte senza confermare l'avvio.
- Le build **Debug** dell'app (flag nell'intestazione) partono senza record e senza conteggio degli avvii, per il
  debug con ST-LINK.
- **In modalità bootloader:** USB CDC con lo stesso VID/PID dell'app (`0483:5740`), prodotto `FAC_V2 Bootloader`;
  uscite dei motori e dei servo a livello basso; LED con due lampeggi brevi al secondo (lampeggia a ogni pacchetto
  durante la scrittura). Se è entrato su richiesta e non riceve comandi per 60 s, torna all'app.
- **Protocollo v1** a pacchetti `A5 cmd seq len payload crc32`: `INFO`, `ERASE`, `WRITE`, `VERIFY`, `BOOT`.

## Struttura

| Percorso | Contenuto |
|---|---|
| `FAC_V2_Bootloader.ioc` | configurazione STM32CubeMX (toolchain **CMake**) |
| `Core/`, `USB_DEVICE/`, `startup_stm32f072xb.s` | codice generato da CubeMX; il nostro codice solo nelle sezioni `USER CODE` |
| `Core/Src/Bootloader/`, `Core/Inc/Bootloader/` | logica del bootloader (nostra) |
| `Core/Inc/Bootloader/fac_boot_shared.h` | indirizzi e strutture comuni: **da copiare identico nel firmware** |
| `fac_bootloader.ld` | linker script (flash 16 KB, RAM da `0x20000100`, blocco informativo a `+0xC0`) |
| `Drivers/`, `Middlewares/` | HAL/LL, CMSIS e USB Device Library di ST |
| `tests/` | test della logica su PC, con flash e periferiche simulate |
| `docker/`, `scripts/` | ambiente di build e script |

La logica accede all'hardware solo tramite `fac_bl_port.h`: sulla scheda lo implementa `fac_bl_port_stm32.c`, nei
test un simulatore. Così protocollo, controlli e avvio si provano senza la scheda.

## Compilare, scrivere e fare il debug in VS Code

1. Installare l'estensione **STM32Cube for Visual Studio Code** (consigliata da `.vscode/extensions.json`).
2. Aprire la cartella e accettare la configurazione CMake proposta dall'estensione (preset `Debug` o `Release`).
3. «FAC V2 Bootloader: build, flash and debug (ST-LINK)» compila, scrive e si ferma su `main`.

La configurazione Debug è compilata con `-Os -g3` (come Release, con i simboli): con `-Og` il bootloader supererebbe
i 16 KB.

## Build e test in Docker

Serve solo Docker. Lo script crea l'immagine (Arm GNU Toolchain 13.3, CMake, Ninja) al primo uso.

```sh
sh scripts/fac-bl.sh build Release   # oppure: build Debug
sh scripts/fac-bl.sh test            # test della logica su PC (UBSan attivo; ASan con -DFAC_TESTS_ASAN=ON)
sh scripts/fac-bl.sh clean
```

Su Windows c'è anche `scripts\fac-bl.ps1` con gli stessi comandi. Il risultato è in `build/<preset>/`:
`FAC_V2_Bootloader.elf`, `.hex`, `.bin`. La build stampa l'occupazione: il bootloader deve stare in 16 KB.

## Dimensione

Il limite di 16 KB è imposto dal linker script (la build fallisce se lo supera). Accorgimenti usati:

- RCC e GPIO con i driver **LL** invece della HAL (scelta nel `.ioc`, Project Manager → Advanced Settings);
- flash cancellata e scritta a registri in `fac_bl_port_stm32.c`;
- `USE_USB_DOUBLE_BUFFER=0` in `CMakeLists.txt`: la classe CDC non usa endpoint a doppio buffer.

## Rigenerare il codice con CubeMX

Il codice è generato da STM32CubeMX 6.15 (FW_F0 V1.11.5) a partire da `FAC_V2_Bootloader.ioc`. Dopo ogni
«Generate Code»:

- controllare che le chiamate `FAC_BL_*` siano rimaste nelle sezioni `USER CODE` di `main.c` e `usbd_cdc_if.c`;
- la build usa `fac_bootloader.ld`; `STM32F072xx_FLASH.ld`, ricreato da CubeMX, non è usato (ed è ignorato da git);
- il CRS (HSI48 sincronizzato sul SOF USB) è attivato da `FAC_BL_early_init()`, non dal `.ioc`;
- ricompilare e controllare l'occupazione (deve restare sotto i 16 KB).

## Versione

`FAC_BL_VERSION_*` in `Core/Inc/Bootloader/fac_bl_config.h`, codificata come la versione del firmware
(`major·10000 + minor·100 + patch`). Finisce nel blocco informativo e nella risposta a `INFO`.

## Licenza

Il codice del progetto è sotto licenza **BSD-3-Clause** (`LICENSE`). I file di STMicroelectronics mantengono le loro
licenze (HAL e CMSIS: BSD-3-Clause e Apache-2.0; USB Device Library: SLA0044, uso consentito solo su dispositivi ST).
