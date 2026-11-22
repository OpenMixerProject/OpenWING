# WING Compact T55 – passives Diagnoseimage

**Nicht als Firmware-Update installieren.** Das Image ist für einen bereits
nachweislich flüchtigen RAM-Start gedacht. Das Repo belegt den Ablauf **ab** dem
Laden durch den Stock-Bootloader: ARM-Einstieg `0x10008000`, eingebettetes U-Boot,
dann automatisch `bootm 0x100C9000` aus RAM (siehe `configs/uboot/mx6d_linux_defconfig`
und den Packer `lib/private/tools/build_linux_wingfw.py`). Ein Bootmedium, eine
Tastenkombination oder ein RAM-only-Start einer `.wingfw` vom USB-Stick sind im
Repo **nicht belegt**. USB-B ist der Konsolenanschluss, nicht das Bootmedium.
Ohne bereits verifizierten RAM-Startweg hier stoppen: kein Stock-Update, kein
Flashen, kein `saveenv` und keine vermutete Recovery-Tastenkombination.

## Am Mac

1. Datei: `build/output-t55-diag/wing-compact-t55-diag.wingfw`. Den ausgelieferten
   SHA-256 vor einem Test mit der `.sha256`-Datei im gleichen Verzeichnis prüfen:

   ```sh
   (cd build/output-t55-diag && shasum -a 256 -c wing-compact-t55-diag.wingfw.sha256)
   ```

2. Ein USB-Datenkabel vom Mac zum **USB-B-Port** der WING Compact anschließen.
   Analoge/digitale Audioausgänge vom restlichen Aufbau trennen; das Diagnoseimage
   setzt keinen Mute-Zustand. Nur über den eigenen verifizierten flüchtigen
   Startweg laden. Es gibt bewusst keine Anzeige-Initialisierung, kein Linux-Login
   und keinen SSH-Zugang.

3. Im OpenWING-Workspace nach der Linux-Enumeration den angebotenen Port prüfen
   und **genau diesen** Pfad eingeben. Bei mehreren Treffern vor/nach Anschluss
   vergleichen; nicht den ersten Treffer blind verwenden.

   ```sh
   python3 tools/wing-t55-diag/record.py --list
   printf 'WING-Port aus der Liste: '; IFS= read -r port
   python3 tools/wing-t55-diag/record.py --port "$port"
   ```

   Python 3 und macOS-Bordmittel genügen. Kein `sudo`, keine Terminal-App parallel
   am Port. Der Recorder liest nur (CDC-Leitungskodierung 115200 8N1), sendet
   keine Nutzdaten und wechselt den Port nicht selbständig. Er zeigt sowohl den
   Empfang als auch den persistenten Pfad `.amp/in/wing-t55-diag/cdc-*.log` an.
   Diesen Pfad für Amp nennen. `Ctrl-C` beendet sauber. Ein abgezogenes Kabel
   oder Reboot beendet die Aufnahme; `--list` und Start manuell wiederholen.

4. Erwartet: in wenigen Sekunden ein vollständiger `WING_DIAG_V1`-Block von
   `event=begin` bis `event=end`, wiederholt etwa alle drei Sekunden. `build=` muss
   zu `build/output-t55-diag/build-id` passen; `boot=` bleibt bis zum Neustart
   gleich. `ecspi2`/`spi_imx` und `usbotg`/`imx_usb` sollen vorhanden sein, die
   Gadget-TTY `ttyGS0` und ein `configured`-UDC während der Aufnahme ebenfalls.
   **Absichtlich abwesend:** `spi1.0`, `spi1.1`, `spidev1.0`, `spidev1.1`,
   `spibone0`. `state=not-probed` sagt nichts über den Zustand von FPGA/DSP aus.

Bei falscher Build-ID, unerwarteten SPI-Knoten, `event=error` oder keinem
vollständigen Block nach 15 Sekunden: anhalten, letzte Ausgabe und Logpfad an
Amp geben. Bei fehlendem Port zuerst USB-B, Datenkabel und `--list` prüfen;
nicht auf eine andere serielle Schnittstelle ausweichen. Nur wenn ein flüchtiger
Start tatsächlich verwendet wurde, ist Aus-/Einschalten der Rückweg zur
unveränderten Firmware. Das ist offline nicht am Pult erprobt. Kein FPGA-/DSP-
Kommando und kein Firmware-Update als Recovery starten.

## Was USB sieht – und was nicht

- Der bestehende Produktpfad wartet in `initramfs_root/init` auf `/dev/ttyGS0`
  und startet dort `getty`. Dieses Image nutzt denselben i.MX6-OTG-Controller
  `2184000.usb` im Peripheral-Modus, ChipIdea-UDC und Linux `g_serial` CDC-ACM
  (`0525:a4a7`), aber sendet nur Diagnoseblöcke. Das Host-Pendant ist
  `/dev/cu.usbmodem…`. Die USB-Host-Controller bleiben im Diagnose-DTB aus.
- Vor Linux-Enumeration kann dieser Recorder nichts sehen. Das eingebettete
  U-Boot bietet gegebenenfalls eine **andere** ACM-Instanz (`0525:a4a5`). Die
  Linux-Frühkonsole ist `serial1` / `ttymxc1` (115200); der Kernel hat keine
  `U_SERIAL_CONSOLE`. Boot-ROM/Stock-Bootloader, U-Boot, Kernel-Decompressor und
  frühe Kernel-Ausgaben werden nicht als USB-Historie nachgereicht. Die ersten
  `/init`-Meilensteine gehen an `/dev/console` und nach `/run/wing-diag-early`;
  sie werden in `/run/wing-diag.latest` mit den späteren Statuswerten auf CDC
  wiederholt. Wenn Linux gar nicht bis `/init` oder zum Gadget kommt, gibt es
  über diesen Mitschnitt keinen Rückschluss auf die genaue Fehlerstelle.
- `spi1` ist ECSPI2 (`200c000.spi`), **ohne** Pinmux, Chip-Select-Zuordnung oder
  Kindgeräte. Der Linux-Treiber betreibt seine SoC-internen Controller-Clocks,
  aber hier wird kein SPI-Transfer gestartet. Keine Programmierung von Board-
  Audio-Clocks, FPGA-Probe/-Load, DSP-Reset/-Boot, FS/VALID oder Unmute.

## Offline bauen, prüfen und wiederverwenden

`bash tools/wing-t55-diag/build.sh [Archiv-Verzeichnis]` nutzt den vorhandenen
Container `wing-usb-console-builder:trixie`, das private Packer-Submodul und
die drei SHA-gepinnten Archive aus `sources.sha256` (Standardcache:
`build/linux-t55-recovery`). Der Container baut ohne Netz in einem separaten
case-sensitiven Volume. Er schreibt nur unter `build/output-t55-diag/`, auch
`wing-t55-diag.itb`, `wing-t55-diag-initramfs.cpio.gz`, `build-id` und
`verification.log`. Prüfen aus der fertigen `.wingfw` und die PTY-Tests starten:

```sh
docker run --rm --network none -v "$PWD:/src:ro" -w /src \
  wing-usb-console-builder:trixie python3 -B tools/wing-t55-diag/verify.py \
  build/output-t55-diag/wing-compact-t55-diag.wingfw
python3 -B tools/wing-t55-diag/test_diag.py -v
```

Die Prüfung umfasst Paket-Wrapper, FIT und CRCs, eingebettete Kernel-Config,
DTB und initramfs-Allowlist. Die PTY-Tests brauchen kein Pult.

Für B-Bench: `tools/wing-t55-diag/wing-t55-diag` unverändert als
`/bin/wing-t55-diag` (0755) installieren und **ohne** `--passive` manuell aufrufen.
Ein Snapshot auf stdout, SPI-Ziele `expected=observe-only`. Benötigt POSIX `sh`
mit `printf`, `test`, `read`, außerdem `cat`, `readlink`, `tr`, `uname`, `/proc`,
`/sys` und `/dev`. Optional `/etc/wing-diag-build` (64 kleingeschriebene Hex-Zeichen
plus LF) und `/run/wing-diag-early` (bereits formatierte Diagnose-Meilensteine,
keine Rohlogs). Der Mac-Recorder ist unverändert nutzbar, wenn der Bench-Aufruf
seinen Snapshot auf dem Gadget ausgibt. Der Recorder selbst startet nichts auf
dem Board; Bench-Init muss den Snapshot getrennt ausgeben, wenn der Recorder
den Port exklusiv offen hält. Nicht mit `wing-t55 load/check/run` verketten.
