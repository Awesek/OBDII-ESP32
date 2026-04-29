#ifndef CONFIG_H
#define CONFIG_H

/** 
 * Globalni Konfigurace (Piny, Site, RTOS Tasky)
*/

/**
 * GPIO piny pro CAN transceiver (SN65HVD230).
 *
 * ESP32 nema vestaveny CAN fyzicky budic — pouziva se externi transceiver
 * SN65HVD230, ktery prevadi logicke urovne (TX/RX) na diferencni signaly
 * CAN_H a CAN_L na sbernici.
 *
 * Mapovani pinu:
 *   - CAN_TX_PIN (GPIO 15): ESP32 → transceiver → CAN_H/CAN_L
 *   - CAN_RX_PIN (GPIO 13): CAN_H/CAN_L → transceiver → ESP32
 *
 * POZOR: Tyto piny musi odpovidat fyzickemu zapojeni.
 * Transceiver SN65HVD230 drzi TX vstup v definovanem stavu, takze to neni problem.
 */
#define CAN_TX_PIN          15
#define CAN_RX_PIN          13

/**
 * Rychlost CAN sbernice v bitech za sekundu.
 *
 * OBD-II standard (ISO 15765-4) vyzaduje 500 kbit/s pro vsechna osobni
 * vozidla vyrobena od roku 2008+ (v EU od 2001 pro benzinova, 2004 pro
 * dieslova). Nektere starsi vozy mohou pouzivat 250 kbit/s — v takovem
 * pripade zmente tuto hodnotu.
 *
 * Priklady:
 *   500000 — standardni OBD-II (vetsina modernich vozidel)
 *   250000 — nektere starsi vozy, uzitková vozidla, nakladni automobily
 *   125000 — zemedelska technika, lodni motory (J1939)
 */
#define CAN_BAUDRATE        500000

/**
 * Nastaveni WiFi Access Pointu (pristupoveho bodu).
 *
 * ESP32 pracuje v rezimu AP (Access Point), coz znamena, ze samo vytvari
 * WiFi sit. Klient (telefon, notebook) se pripoji primo k ESP32 bez
 * nutnosti externi infrastruktury (routeru, internetu).
 *
 * Parametry:
 *   WIFI_SSID a WIFI_PASSWORD jsou definovany v secrets.h
 *   WIFI_CHANNEL      — WiFi kanal (1-13). Kanal 6 je zvolen jako kompromis.
 *   WIFI_MAX_CLIENTS  — maximalni pocet soucasne pripojenych klientu.
 */
#define WIFI_CHANNEL        6
#define WIFI_MAX_CLIENTS    2

/**
 * Port HTTP serveru.
 *
 * Standardni port 80 umoznuje pristup bez zadavani portu v URL
 * (staci http://192.168.4.1/ misto http://192.168.4.1:80/).
 */
#define HTTP_PORT           80

/**
 * Velikosti FreeRTOS front pro komunikaci mezi tasky.
 *
 * Fronty slouzi jako mezipameti (buffery) mezi producentem a konzumentem:
 *
 * OBD_REQUEST_QUEUE_SIZE (5):
 *   Fronta pro pozadavky od WebSocket klienta smerem k OBD tasku.
 *
 * OBD_RESPONSE_QUEUE_SIZE (20):
 *   Fronta pro odpovedi z OBD tasku zpet ke klientovi. Vetsi nez request
 *   fronta ze dvou duvodu:
 *   a) Streaming mod generuje odpovedi prubezne
 *   b) Multi-PID prikaz generuje velkou odpoved
 */
#define OBD_REQUEST_QUEUE_SIZE   5
#define OBD_RESPONSE_QUEUE_SIZE  20

/**
 * Velikosti zasobniku (stacku) pro FreeRTOS tasky v bajtech.
 *
 * OBD_TASK_STACK_SIZE (8192 B = 8 KB):
 *   OBD task pouziva relativne velke lokalni promenne a hlubsi call stack
 *   (obd2_* volaji isotp_* volaji twai_*).
 *
 * RESPONSE_TASK_STACK_SIZE (6144 B = 6 KB):
 *   Response dispatch task je jednodussi, ale musi pojmout ArduinoJson.
 */
#define OBD_TASK_STACK_SIZE      8192
#define RESPONSE_TASK_STACK_SIZE 6144

/* ========================================================================= */
/*  OBD-II a ISO-TP Nastaveni (Knihovny)                                     */
/* ========================================================================= */

/**
 * Vychozi casovy limit pro odpoved ridici jednotky (v milisekundach).
 *
 * Dle normy ISO 15031-5 je maximalni doba odezvy P2CAN = 50 ms,
 * ale v praxi nektere ridici jednotky (zejmena starsi nebo vytizene)
 * potrebuji vice casu. Hodnota 2000 ms poskytuje dostatecnou rezervu
 * i pro pomale ridici jednotky, multi-frame odpovedi pres ISO-TP
 * a situace s vysokym zatizenim CAN sbernice.
 */
#define OBD2_DEFAULT_TIMEOUT_MS     2000

/**
 * Maximalni pocet diagnostickych poruchovych kodu (DTC), ktere lze
 * ulozit pri jednom cteni (Mode 03 nebo Mode 07).
 *
 * Kazdy DTC zabira 2 bajty v odpovedi. Pri 126 DTC je to 252 bajtu,
 * coz se bezpecne vejde do jednoho bajtu pro citac (max 255).
 * V praxi vozidla zridka obsahuji vice nez 20-30 aktivnich DTC,
 * takze 126 je vice nez dostatecna rezerva.
 */
#define OBD2_MAX_DTC_COUNT          126

/**
 * Maximalni uroven logovani pro OBD2 nastavena v dobe prekladu.
 *
 * Hierarchie urovni (od nejdulezitejsich):
 *   ISOTP_LOG_NONE  (0) — zadne logy
 *   ISOTP_LOG_ERROR (1) — pouze chyby (selhani komunikace, neplatne odpovedi)
 *   ISOTP_LOG_WARN  (2) — varovani (timeout, nepodporovany PID)
 *   ISOTP_LOG_INFO  (3) — informacni zpravy (uspesne operace, VIN, DTC)
 *   ISOTP_LOG_DEBUG (4) — ladici detaily (obsah pozadavku a odpovedi)
 *   ISOTP_LOG_TRACE (5) — maximalni detail (kazdy bajt, casovani)
 *
 * Zpravy s urovni vyssi nez tato hodnota jsou zcela odstraneny
 * prekladacem (nulovy overhead).
 */
#define OBD2_LOG_MAX_LEVEL          3

/**
 * @brief Maximální velikost ISO-TP payloadu v bajtech.
 *
 * Teoretický limit ISO-TP je 4095 B, pro OBD-II ale stačí méně:
 *   - VIN (Mode 09 PID 02) = 20 B
 *   - 126 DTC kódů (Mode 03) = 252 B
 * Hodnota 256 B je bezpečný kompromis mezi pamětí a funkčností.
 */
#define ISOTP_MAX_PAYLOAD           256

/**
 * @brief Výplňový bajt pro nevyužité pozice v CAN rámci.
 *
 * CAN rámec má pevnou délku 8 B; pokud ISO-TP naplní méně, zbylé
 * bajty se vyplňují konstantou. Hodnota 0xCC je běžná konvence
 * (používá ji například ELM327).
 */
#define ISOTP_PADDING_BYTE          0xCC

/**
 * @brief Velikost TWAI RX fronty (počet CAN rámců).
 *
 * Na reálné CAN sběrnici v autě (500 kbit/s) může být 50+ non-OBD rámců
 * za sekundu (ABS, airbag, klima, BCM...). Když je fronta plná, TWAI
 * driver nové rámce tiše zahazuje — mezi nimi může být i aktuální OBD
 * odpověď.
 *
 * Každý slot zabírá ~16 bajtů (twai_message_t). Paměťová náročnost:
 *   32 slotů =  ~512 B  (nedostatečné pro vytíženou sběrnici)
 *   64 slotů = ~1024 B  (bezpečné minimum pro OBD-II)
 *  128 slotů = ~2048 B  (pro velmi zatížené sběrnice)
 *
 * Výchozí hodnota 64 je kompromis mezi spolehlivostí a spotřebou RAM
 * na ESP32 (520 KB SRAM).
 */
#define ISOTP_TWAI_RX_QUEUE_LEN     64

/**
 * @brief Maximální úroveň logování zapečená v době překladu.
 *
 * Zprávy s vyšší úrovní jsou při kompilaci odstraněny (šetří flash).
 */
#define ISOTP_LOG_MAX_LEVEL         3

#endif /* CONFIG_H */
