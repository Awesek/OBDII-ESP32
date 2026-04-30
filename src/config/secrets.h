/**
 * @file secrets.h
 * @brief Uloziste citlivych udaju (credentials, tokeny)
 *
 * Tento soubor shromazduje citlive konfiguracni udaje na jednom
 * miste, aby se:
 *   1. nemusi se hledat, kde je jake heslo definovano
 *   2. dalo jednoduse vymenit za realne hodnoty
 *   3. dal pripadne vyjmout z verzovaciho systemu
 *
 * Obsah:
 *   - Udaje pro WiFi Access Point vytvareny ESP32 (SSID, heslo)
 *   - Autentizacni token pro WebSocket prikazy
 *
 * Pouziti: #include "secrets.h" v kterekoli jednotce, ktera potrebuje
 * pristup k danym udajum.
 *
 * @author Ales Pouzar
 */

#ifndef SECRETS_H
#define SECRETS_H

/* ========================================================================= */
/*  WiFi Access Point                                                        */
/* ========================================================================= */

/**
 * SSID (nazev) vytvarene WiFi site.
 *
 * Toto jmeno se zobrazi v seznamu dostupnych siti na telefonu/notebooku.
 * Delka maximalne 32 znaku (limit standardu 802.11). Pouzivat
 * jen ASCII znaky, aby se sit korektne zobrazila na vsech zarizenich.
 */
#define WIFI_SSID           "OBD2-Diagnostics"

/**
 * Heslo pro WPA2-PSK sifrovani WiFi site.
 *
 * Delka musi byt v rozsahu 8-63 znaku (WPA2 pozadavek). Pro vyssi
 * bezpecnost pouzijte kombinaci pismen, cislic a specialnich znaku.
 */
#define WIFI_PASSWORD       "obd2pass123"

/* ========================================================================= */
/*  WebSocket autentizace                                                    */
/* ========================================================================= */

/**
 * Autentizacni token pro destruktivni WebSocket prikazy.
 *
 * Pouziva se u prikazu, ktere trvale zmeni stav vozidla (napr. smazani
 * DTC, reset readiness monitoru). Klient musi poslat tento token v poli
 * "token" JSON pozadavku, jinak server vrati chybu AUTH_INVALID.
 *
 * Priklad validniho pozadavku:
 *   {"cmd":"clear_dtc","token":"CHANGE_ME_1234"}
 *
 * Max. delka tokenu je 15 znaku (16 vcetne '\0' v strukture
 * obd_request_msg_t). Pri delsich tokenech dojde k orezani.
 *
 * DULEZITE:
 *   - Vychozi hodnota "CHANGE_ME_1234"
 *   - WiFi komunikace je sifrovana WPA2, ale token cestuje v plaintextu
 *     v ramci WebSocket zpravy. V tomto projektu to staci, protoze sit je
 *     lokalni a pouze pro autorizovaneho uzivatele. Pro verejne nasazeni
 *     by bylo nutne pridat TLS (wss://) nebo challenge-response schema.
 */
#define WS_AUTH_TOKEN       "CHANGE_ME_1234"

/**
 * Maximalni delka tokenu vcetne ukoncovaciho '\0'.
 *
 * Pouziva se pro velikost pole char token[] ve strukture
 * obd_request_msg_t. Hodnota 16 je kompromis mezi:
 *   - dostatecnou delkou pro rozumne silny token (15 znaku)
 *   - malou pametovou stopou ve FreeRTOS fronte
 *
 * Pri zmene teto konstanty nezapomente prekompilovat cely projekt,
 * protoze velikost obd_request_msg_t se zmeni.
 */
#define WS_AUTH_TOKEN_MAX   16

#endif /* SECRETS_H */
