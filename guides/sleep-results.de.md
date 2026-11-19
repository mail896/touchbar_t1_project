# MacBookPro14,2 unter Debian 13 · Abschluss der Schlafdiagnose

Am 19. November 2026 blieb Kernel `6.12.116+deb13-amd64` mit s2idle der Standard. Die eingesetzten Versionen des Sleep-Hooks und seiner Konfiguration entsprechen diesem Repository. WLAN-Firmware-Suspend mit eingebundenem brcmfmac und D0-Schutz, beide USB-C-/Thunderbolt-Treiber, abgesichertes ASPM L1 und die Apple-NVMe-Sperre für D3cold bleiben aktiv. Das XHC1-Weckrecht und der reguläre T1-Pfad bleiben erhalten. ECSS, gezieltes Abschalten von Links oder VT-d sowie der RP01-D3-Versuch gehören zu den Testvarianten.

| Messung | Resultat |
| --- | --- |
| Eine Stunde auf Akku, WLAN eingebunden | 2,56 W im späten und 2,77 W im frühen korrigierten Auswertefenster |
| Eine Stunde auf Akku, WLAN abgekoppelt | 3,08 W bzw. 3,20 W mit derselben Auswertung |
| Intel-Energiezähler mit eingebundenem WLAN | CPU-Paket 0,166 W, DRAM 0,256 W, PC10 99,78 % |
| USB-RAM-Kontrolle G am Netzteil | RP01 D0, rund 59,42 s, PC10 99,78 %, PCH-SLP_S0 +0 µs |
| USB-RAM-Lauf H am Netzteil | RP01 D3hot an allen vier Messpunkten, rund 59,27 s, PC10 99,80 %, PCH-SLP_S0 +0 µs |
| macOS auf demselben Gerät | Akkuschätzung rund 0,89 W, methodenabhängig. Separater Deckellauf mit 84,0836 s PCH-SLP_S0 |

Die bekannten RP01-Firmwarezugriffe waren in Lauf H sichtbar. Beim Aufwachen lief _PS0 und PCI meldete D0. Der NVMe-Controller erreichte innerhalb von 35 Sekunden keinen betriebsbereiten Zustand. Die isolierte RAM-Umgebung konnte das Protokoll sichern. Eine technische Motivation zum Aufheben des Schutzes am internen Startlaufwerk ergibt sich daraus nicht.

Auch bei beidseitiger Thunderbolt-Taktfreigabe, getesteter RP12-Sequenz, SOCW ohne nachträglichen Displayzugriff und funktional abgeschaltetem DMAR0 blieb die Debian-Residency in den jeweiligen Versuchen null. Die Versuche fanden mit unterschiedlichen Einstellungen statt. Der Verbrauch der übrigen Plattform ist keinem einzelnen Gerät zugeordnet. Wechselwirkungen zwischen Firmware, Treibern und Schutzregeln bleiben offen.

Das Ziel bleibt ein deutlich niedrigerer realer Akkuverbrauch bei zuverlässigem Wiederanlauf. Die private Übergabe und die zugehörigen Originalmessungen stehen auf dem Transfer-Volume unter `Debian-Schlaf-2026-11-19/UEBERGABE.txt`. Die bestehenden Apple-Ergebnisse liegen daneben unter `macOS/Ergebnisse/`.

Der Ordner `docs/sleep/` enthält die geprüften gerätespezifischen Regeln, den Vorbereitungshook, seinen Dienst und die beiden eingesetzten Debian-Treiberpatches als Referenz.
