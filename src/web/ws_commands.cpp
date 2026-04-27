/**
 * @file ws_commands.cpp
 * @brief Implementace vsech OBD command handleru pro WebSocket rozhrani
 *
 * Tento soubor obsahuje vsechny _ws_cmd_* funkce — kazda zpracovava
 * jeden typ OBD-II diagnostickeho prikazu prijateho pres WebSocket.
 *
 * Architektura:
 *   ws_handler.cpp (dispatch switch) vola _ws_cmd_* funkce z tohoto souboru.
 *   Kazdy handler:
 *     1. Vytvori ArduinoJson dokument
 *     2. Zkontroluje _obd_initialized (guard)
 *     3. Zavola prislusne obd2_* funkce z C vrstvy
 *     4. Naplni JSON dokument vysledky
 *     5. Serializuje dokument do resp->json pomoci _ws_serialize()
 *
 * Funkce _ws_set_error a _ws_serialize jsou definovane v ws_handler.cpp
 * a přistupuje se k nim pres extern deklarace nize.
 *
 * Promenne _obd_initialized a _stream_cfg jsou rovnez v ws_handler.cpp
 * a přistupuje se k nim pres extern.
 *
 * Vsechny funkce v tomto souboru NEJSOU static — jsou volane
 * z dispatch switche ve ws_process_obd_command() v ws_handler.cpp.
 *
 * @see ws_handler.cpp pro dispatch logiku a infrastrukturu
 * @see ws_handler.h pro dokumentaci verejneho API a datove typy
 */

#include "ws_handler.h"

extern "C" {
    #include "obd2.h"
}

#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_system.h"
#include "driver/twai.h"   /* Pro TWAI diagnostiku v _ws_cmd_init */

/* ========================================================================= */
/*  Extern deklarace — pristup ke sdilenym promennym a funkcim               */
/*  z ws_handler.cpp                                                          */
/* ========================================================================= */

/**
 * Pomocna funkce pro vlozeni chybovych poli do JSON dokumentu.
 * Definovana v ws_handler.cpp. Pouziva se ve vsech handlerech
 * pri chybe obd2_* volani (timeout, negative response, atd.).
 *
 * Priklad pouziti:
 *   _ws_set_error(doc, OBD2_ERR_TIMEOUT, "ECU neodpovida");
 *   → doc bude obsahovat {"status":"error","error":"TIMEOUT","message":"ECU neodpovida"}
 */
extern void _ws_set_error(JsonDocument &doc, obd2_status_t obd_st,
                          const char *message);

/**
 * Pomocna funkce pro serializaci JSON dokumentu do response bufferu.
 * Definovana v ws_handler.cpp. Kontroluje preteceni bufferu.
 *
 * Priklad pouziti:
 *   _ws_serialize(doc, resp);
 *   → resp->json bude obsahovat serializovany JSON retezec
 */
extern void _ws_serialize(JsonDocument &doc, obd_response_msg_t *resp);

/**
 * Stav OBD inicializace — true po uspesnem CMD_INIT.
 * Definovana v ws_handler.cpp. Pouziva se jako guard ve vsech
 * handlerech krome _ws_cmd_ping (ten OBD vrstvu nepotrebuje).
 *
 * Zapisuje se pouze v _ws_cmd_init (pri uspechu → true).
 * Cte se ve vsech ostatnich handlerech pro kontrolu inicializace.
 */
extern volatile bool _obd_initialized;

/**
 * Konfigurace streamu (aktivita, PIDy, interval).
 * Definovana v ws_handler.cpp. Typ ws_stream_cfg_t je v ws_handler.h.
 */
extern volatile ws_stream_cfg_t _stream_cfg;

#include "config.h"

static void _ws_add_init_diag(JsonDocument &doc)
{
    const obd2_init_diag_t *diag = obd2_get_init_diag();
    JsonObject d = doc["diag"].to<JsonObject>();

    d["twai_state"] = diag->twai_state;
    d["tec"] = diag->tx_error_counter;
    d["rec"] = diag->rx_error_counter;
    d["msgs_to_tx"] = diag->msgs_to_tx;
    d["msgs_to_rx"] = diag->msgs_to_rx;
    d["attempts"] = diag->init_attempts;
    d["used_physical_fallback"] = diag->used_physical_fallback;
    d["reinit_performed"] = diag->reinit_performed;
    d["last_isotp_status"] = isotp_status_str(diag->last_isotp_status);
    d["last_obd_status"] = obd2_status_str(diag->last_obd_status);

    char alerts_hex[12];
    snprintf(alerts_hex, sizeof(alerts_hex), "0x%08lX",
             (unsigned long)diag->alerts);
    d["alerts"] = alerts_hex;

    char tx_hex[12];
    char rx_hex[12];
    snprintf(tx_hex, sizeof(tx_hex), "0x%03lX", (unsigned long)diag->last_tx_id);
    snprintf(rx_hex, sizeof(rx_hex), "0x%03lX", (unsigned long)diag->last_rx_id);
    d["last_tx_id"] = tx_hex;
    d["last_rx_id"] = rx_hex;
}

static obd2_status_t _ws_start_obd_transport(void)
{
    obd2_status_t st = obd2_init(CAN_BAUDRATE, CAN_TX_PIN, CAN_RX_PIN);
    if (st != OBD2_OK) return st;

    obd2_set_timeout(2000);
    obd2_set_log_level(ISOTP_LOG_INFO);
    isotp_set_log_level(ISOTP_LOG_INFO);
    return OBD2_OK;
}

static void _ws_add_active_ecu(JsonDocument &doc)
{
    uint32_t tx_id = 0;
    uint32_t rx_id = 0;
    bool bound = obd2_get_active_ecu(&tx_id, &rx_id);

    JsonObject active = doc["active_ecu"].to<JsonObject>();
    active["bound"] = bound;

    char tx_hex[12];
    char rx_hex[12];
    snprintf(tx_hex, sizeof(tx_hex), "0x%03lX", (unsigned long)tx_id);
    snprintf(rx_hex, sizeof(rx_hex), "0x%03lX", (unsigned long)rx_id);
    active["tx_id"] = tx_hex;
    active["rx_id"] = rx_hex;
}

static void _ws_add_pid00_responses(JsonDocument &doc, const isotp_result_t *result)
{
    JsonArray responses = doc["responses"].to<JsonArray>();
    if (result == NULL) {
        doc["response_count"] = 0;
        return;
    }

    for (uint8_t i = 0; i < result->count; i++) {
        const isotp_response_t *r = &result->responses[i];
        JsonObject item = responses.add<JsonObject>();

        char id_str[12];
        snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)r->rx_id);
        item["rx_id"] = id_str;
        item["valid"] = r->valid;
        item["len"] = r->len;

        char payload[128] = "";
        int pos = 0;
        for (uint16_t b = 0; b < r->len && b < 32; b++) {
            int written = snprintf(payload + pos, sizeof(payload) - pos,
                                   "%02X%s", r->data[b],
                                   (b + 1 < r->len && b < 31) ? " " : "");
            if (written < 0 || written >= (int)(sizeof(payload) - pos)) break;
            pos += written;
        }
        item["payload"] = payload;

        if (r->valid && r->len >= 6 &&
            r->data[0] == (OBD2_SID_CURRENT_DATA + OBD2_SID_RESPONSE_OFFSET) &&
            r->data[1] == 0x00) {
            uint32_t bitmask = ((uint32_t)r->data[2] << 24) |
                               ((uint32_t)r->data[3] << 16) |
                               ((uint32_t)r->data[4] << 8)  |
                               ((uint32_t)r->data[5]);
            char mask_str[12];
            snprintf(mask_str, sizeof(mask_str), "0x%08lX", (unsigned long)bitmask);
            item["pid00_mask"] = mask_str;
        }
    }
    doc["response_count"] = result->count;
}

static void _ws_format_pid_raw_hex(const obd2_pid_decoded_t &decoded,
                                   char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return;
    size_t off = 0;
    int written = snprintf(out, out_len, "0x");
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    off = (size_t)written;

    for (uint8_t i = 0; i < decoded.raw_data_len; i++) {
        if (off + 3 > out_len) break;
        written = snprintf(out + off, out_len - off, "%02X", decoded.raw_data[i]);
        if (written < 0) break;
        off += (size_t)written;
    }
}

static void _ws_bytes_to_hex(const uint8_t *data, uint16_t len,
                             char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return;
    out[0] = '\0';
    if (data == NULL) return;

    size_t off = 0;
    for (uint16_t i = 0; i < len; i++) {
        int written = snprintf(out + off, out_len - off, "%02X%s",
                               data[i], (i + 1 < len) ? " " : "");
        if (written < 0 || written >= (int)(out_len - off)) {
            if (out_len > 0) out[out_len - 1] = '\0';
            break;
        }
        off += (size_t)written;
    }
}

static const char *_ws_ipt_counter_name(uint8_t infotype, uint8_t index)
{
    static const char *spark[] = {
        "OBDCOND", "IGNCNTR", "CATCOMP1", "CATCOND1", "CATCOMP2",
        "CATCOND2", "O2SCOMP1", "O2SCOND1", "O2SCOMP2", "O2SCOND2",
        "EGRCOMP", "EGRCOND", "AIRCOMP", "AIRCOND", "EVAPCOMP",
        "EVAPCOND", "SO2SCOMP1", "SO2SCOND1", "SO2SCOMP2", "SO2SCOND2"
    };
    static const char *compression[] = {
        "OBDCOND", "IGNCNTR", "HCCATCOMP", "HCCATCOND", "NCATCOMP",
        "NCATCOND", "NADSCOMP", "NADSCOND", "PMCOMP", "PMCOND",
        "EGSCOMP", "EGSCOND", "EGRCOMP", "EGRCOND", "BPCOMP",
        "BPCOND", "FUELCOMP", "FUELCOND"
    };

    if (infotype == OBD2_INFOTYPE_IPT_COMPRESSION) {
        return (index < (sizeof(compression) / sizeof(compression[0])))
                   ? compression[index]
                   : "COUNTER";
    }
    return (index < (sizeof(spark) / sizeof(spark[0]))) ? spark[index]
                                                         : "COUNTER";
}

static const char *_ws_stream_mode_str(ws_stream_mode_t mode)
{
    return (mode == WS_STREAM_MODE_INSPECTOR) ? "inspector" : "dash";
}

static void _ws_add_pid_diag(JsonObject obj,
                             const obd2_pid_decoded_t *decoded,
                             obd2_status_t st)
{
    char tx_hex[8], rx_hex[8];
    const obd2_init_diag_t *diag = obd2_get_init_diag();
    snprintf(tx_hex, sizeof(tx_hex), "0x%03lX", (unsigned long)diag->last_tx_id);
    snprintf(rx_hex, sizeof(rx_hex), "0x%03lX", (unsigned long)diag->last_rx_id);

    obj["obd_status"] = obd2_status_str(st);
    obj["isotp_status"] = isotp_status_str(diag->last_isotp_status);
    obj["tx_id"] = tx_hex;
    obj["rx_id"] = rx_hex;

    if (decoded != NULL) {
        char raw_hex[2 + OBD2_PID_MAX_DATA_BYTES * 2 + 1];
        _ws_format_pid_raw_hex(*decoded, raw_hex, sizeof(raw_hex));
        obj["raw"] = raw_hex;
        obj["raw_len"] = decoded->raw_data_len;
    } else {
        obj["raw"] = "0x";
        obj["raw_len"] = 0;
    }
}

/* ========================================================================= */
/*  Command handlery — kazdy prikaz ma svoji funkci                          */
/* ========================================================================= */

/**
 * @brief CMD_PING — test zivosti spojeni s ESP32.
 *
 * Nepotrebuje OBD vrstvu ani CAN komunikaci. Odpovida okamzite
 * s uzitecnymi informacemi o stavu systemu:
 *   - free_heap: volna pamet na heapu (pro detekci memory leaku)
 *   - uptime_ms: cas od startu ESP32 (pro detekci resetu)
 *   - obd_init: zda je OBD vrstva inicializovana
 *
 * Priklad odpovedi:
 *   {"cmd":"ping","status":"ok","free_heap":245760,"uptime_ms":12345,"obd_init":true}
 *
 * Hranicni pripady:
 *   - Muze byt volano pred CMD_INIT — obd_init bude false
 *   - Muze byt volano i kdyz je OBD task zaneprazdnen (zpracovava se na Core 0)
 */
void _ws_cmd_ping(const obd_request_msg_t *req,
                         obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"]       = "ping";
    doc["status"]    = "ok";
    doc["free_heap"] = (uint32_t)esp_get_free_heap_size();
    doc["uptime_ms"] = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    doc["uptime_s"]  = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    doc["obd_init"]  = _obd_initialized;
    doc["transport_ready"] = obd2_is_transport_initialized();
    if (req->hb) doc["hb"] = true;
    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_INIT — inicializace cele OBD-II diagnosticke vrstvy.
 *
 * Sekvence inicializace:
 *   1. obd2_init(baudrate, tx_pin, rx_pin) — inicializuje TWAI driver
 *      a ISO-TP vrstvu. Pokud uz je inicializovano, obd2_init to detekuje
 *      a vrati OK (bezpecne opetovne volani).
 *   2. obd2_set_timeout(2000) — nastavi timeout na 2000ms.
 *      2 sekundy je dostatecne i pro Mode 09 (VIN, ECU name) kde
 *      ECU odpovida multi-frame zpravou pres ISO-TP.
 *   3. obd2_set_log_level + isotp_set_log_level — snizeni logovani
 *      pro produkci (INFO misto TRACE, ktere by zaplavilo serial).
 *   4. obd2_query_supported_pids() — broadcast discovery na 0x7DF,
 *      ECU odpovi s bitmask podporovanych PIDu ktere se ulozi do cache.
 *
 * Pri selhani kterehokoliv kroku vraci chybu a _obd_initialized zustane false.
 *
 * Priklad uspesne odpovedi:
 *   {"cmd":"init","status":"ok","supported_pids":[4,5,12,13,...],"pid_count":28}
 *
 * Priklad chybove odpovedi (napr. CAN sbernice neni pripojena):
 *   {"cmd":"init","status":"error","error":"TIMEOUT","message":"query_supported_pids selhalo"}
 */
void _ws_cmd_transport_init(const obd_request_msg_t *req,
                            obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "transport_init";

    obd2_status_t st = _ws_start_obd_transport();
    doc["transport_ready"] = obd2_is_transport_initialized();
    doc["obd_ready"] = _obd_initialized;
    _ws_add_init_diag(doc);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "transport init failed");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    doc["baudrate"] = CAN_BAUDRATE;
    doc["tx_pin"] = CAN_TX_PIN;
    doc["rx_pin"] = CAN_RX_PIN;
    _ws_add_active_ecu(doc);
    _ws_serialize(doc, resp);
}

void _ws_cmd_pid00_probe(const obd_request_msg_t *req,
                         obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "pid00_probe";

    obd2_status_t st = _ws_start_obd_transport();
    if (st != OBD2_OK) {
        doc["transport_ready"] = obd2_is_transport_initialized();
        doc["obd_ready"] = _obd_initialized;
        _ws_set_error(doc, st, "transport init failed");
        _ws_add_init_diag(doc);
        _ws_serialize(doc, resp);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    static isotp_result_t result;
    st = obd2_probe_pid00(&result);
    doc["transport_ready"] = obd2_is_transport_initialized();
    doc["obd_ready"] = _obd_initialized;
    _ws_add_init_diag(doc);
    _ws_add_pid00_responses(doc, &result);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "PID00 probe failed");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    _ws_serialize(doc, resp);
}

void _ws_cmd_init(const obd_request_msg_t *req,
                         obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "init";
    _obd_initialized = false;

    /* Inicializace OBD vrstvy (ta internne vola isotp_init) */
    obd2_status_t st = _ws_start_obd_transport();
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "obd2_init selhalo");
        doc["transport_ready"] = obd2_is_transport_initialized();
        doc["obd_ready"] = false;
        _ws_add_init_diag(doc);
        _ws_serialize(doc, resp);
        return;
    }

    /* Nastaveni timeoutu — 2000ms je dostatecne i pro Mode 09 */
    obd2_set_timeout(2000);

    /* Snizeni urovne logovani pro produkci (INFO misto TRACE) */
    obd2_set_log_level(ISOTP_LOG_INFO);
    isotp_set_log_level(ISOTP_LOG_INFO);

    /* Stabilizacni pauza po TWAI init — CAN transceiver a ECU potrebuji
     * cas na synchronizaci po reinstalaci driveru. CAN controller musi
     * videt 11 po sobe jdoucich recesivnich bitu (Bus Idle) nez muze
     * vysilat. Na aktivni sbernici to trva typicky < 1ms, ale po
     * reinstalaci driveru muze trvat dele kvuli error recovery a
     * synchronizaci bit-timingu. 300ms je konzervativni hodnota. */
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Vyprazdneni RX fronty od ramcu prijatych behem stabilizacni pauzy.
     * Na aktivnim CAN busu (motor bezi) se behem 300ms nahromadi desitky
     * az stovky ramcu ktere by zpomalily nasledny broadcast. */
    {
        twai_message_t dummy;
        int drain_count = 0;
        while (twai_receive(&dummy, 0) == ESP_OK) { drain_count++; }
        if (drain_count > 0) {
            Serial.printf("[INIT ] Drained %d frames from RX queue after stabilization\n", drain_count);
        }
    }

    /* TWAI diagnostika pred broadcast — log stavu sbernice pro debugging.
     * Pokud TEC/REC nejsou 0, sbernice mela problemy behem stabilizace. */
    {
        twai_status_info_t twai_st;
        if (twai_get_status_info(&twai_st) == ESP_OK) {
            Serial.printf("[INIT ] TWAI pre-query: state=%d TEC=%d REC=%d msgs_to_rx=%d\n",
                          (int)twai_st.state,
                          (int)twai_st.tx_error_counter,
                          (int)twai_st.rx_error_counter,
                          (int)twai_st.msgs_to_rx);
        }

        /* Kontrola TWAI alertu — detekce bus errors behem stabilizace */
        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts != 0) {
            Serial.printf("[INIT ] TWAI alerts: 0x%08lX%s%s%s%s%s\n",
                          (unsigned long)alerts,
                          (alerts & TWAI_ALERT_BUS_OFF)       ? " BUS_OFF"       : "",
                          (alerts & TWAI_ALERT_ERR_PASS)      ? " ERR_PASS"      : "",
                          (alerts & TWAI_ALERT_BUS_ERROR)     ? " BUS_ERR"       : "",
                          (alerts & TWAI_ALERT_TX_FAILED)     ? " TX_FAIL"       : "",
                          (alerts & TWAI_ALERT_RX_QUEUE_FULL) ? " RX_Q_FULL"     : "");
        }
    }

    /* Discovery podporovanych PIDu (broadcast na 0x7DF) */
    st = obd2_query_supported_pids();
    if (st != OBD2_OK) {
        /* Diagnostika pri selhani — zalogujeme TWAI stav pro dalsi analyzu */
        twai_status_info_t fail_st;
        if (twai_get_status_info(&fail_st) == ESP_OK) {
            Serial.printf("[INIT ] TWAI post-failure: state=%d TEC=%d REC=%d\n",
                          (int)fail_st.state,
                          (int)fail_st.tx_error_counter,
                          (int)fail_st.rx_error_counter);
        }
        uint32_t fail_alerts = 0;
        if (twai_read_alerts(&fail_alerts, 0) == ESP_OK && fail_alerts != 0) {
            Serial.printf("[INIT ] TWAI failure alerts: 0x%08lX\n",
                          (unsigned long)fail_alerts);
        }
        _ws_set_error(doc, st, "query_supported_pids selhalo");
        doc["transport_ready"] = obd2_is_transport_initialized();
        doc["obd_ready"] = false;
        _ws_add_init_diag(doc);
        _ws_serialize(doc, resp);
        return;
    }

    _obd_initialized = true;

    /* Sestaveni seznamu podporovanych PIDu — odesilame jako CISLA (uint8_t),
     * ne jako hex stringy. Frontend si format "0xNN" generuje sam pres
     * pomocnou funkci pidToHex(). Drive odesilane stringy zpusobovaly chybu
     * "0x0X01" v UI, kdyz se na string aplikoval toString(16).toUpperCase().
     *
     * Krome jednotneho seznamu posilame i kategorizovane podseznamy podle
     * obd2_pid_category_t — frontend je pouziva pro:
     *   telemetry_pids = vychozi vyber pro stream + DASH bubliny
     *   status_pids    = "Status snapshot" v Diag panelu (on-demand cteni)
     *   config_pids    = Vehicle Info na HOME (cte se pouze 1x po init)
     */
    doc["status"] = "ok";
    doc["transport_ready"] = true;
    doc["obd_ready"] = true;
    _ws_add_active_ecu(doc);
    JsonArray pids = doc["supported_pids"].to<JsonArray>();
    JsonArray telemetry_pids = doc["telemetry_pids"].to<JsonArray>();
    JsonArray status_pids = doc["status_pids"].to<JsonArray>();
    JsonArray config_pids = doc["config_pids"].to<JsonArray>();

    for (uint16_t pid = 0x01; pid <= 0xFF; pid++) {
        if (!obd2_is_pid_supported((uint8_t)pid)) continue;
        pids.add((uint8_t)pid);

        const obd2_pid_desc_t *desc = obd2_get_pid_descriptor((uint8_t)pid);
        if (!desc) continue;
        switch (desc->category) {
        case OBD2_CAT_TELEMETRY: telemetry_pids.add((uint8_t)pid); break;
        case OBD2_CAT_STATUS:    status_pids.add((uint8_t)pid);    break;
        case OBD2_CAT_CONFIG:    config_pids.add((uint8_t)pid);    break;
        case OBD2_CAT_META:      /* skip */                         break;
        }
    }
    doc["pid_count"] = pids.size();

    /*
     * Vehicle Info — staticka data o vozidle, ktera se nameni za behu.
     * Cteme je hned po init, klient je zobrazi v pruhu na HOME.
     * Posilame jen ty CONFIG PIDy ktere ECU skutecne podporuje. Pro neznamy
     * format (napr. NRC) nezarazujeme — chyba se nezahazi do logu.
     */
    JsonObject vinfo = doc["vehicle_info"].to<JsonObject>();
    for (JsonVariant v : config_pids) {
        uint8_t pid = (uint8_t)v.as<int>();
        obd2_pid_decoded_t decoded;
        if (obd2_get_pid(pid, &decoded) != OBD2_OK) continue;

        const obd2_pid_desc_t *desc = obd2_get_pid_descriptor(pid);
        if (!desc) continue;

        /* Vehicle info klic je decimalni PID jako string ("28" pro $1C, "81" pro $51).
         * Frontend si nazev a vals[] mapuje pres lokalni PID_INFO tabulku. */
        char key[4];
        snprintf(key, sizeof(key), "%u", pid);

        if (desc->format == OBD2_FMT_ENUM ||
            desc->format == OBD2_FMT_BIT_ENCODED ||
            desc->format == OBD2_FMT_CONFIG ||
            desc->format == OBD2_FMT_RAW) {
            char hex[2 + OBD2_PID_MAX_DATA_BYTES * 2 + 1];
            _ws_format_pid_raw_hex(decoded, hex, sizeof(hex));
            vinfo[key] = hex;
        } else {
            vinfo[key] = (double)decoded.value;
        }
    }

    /* Detected ECU — seznam jednotek detekovanych pri broadcast PID $00.
     * Frontend muze zobrazit pocet ECU a jejich CAN ID hned po init,
     * bez nutnosti volat discover_ecus zvlast. */
    const obd2_detected_ecu_list_t *detected = obd2_get_detected_ecus();
    uint32_t active_tx = 0;
    uint32_t active_rx = 0;
    bool active_bound = obd2_get_active_ecu(&active_tx, &active_rx);
    if (detected->count > 0) {
        JsonArray ecu_arr = doc["detected_ecus"].to<JsonArray>();
        for (uint8_t i = 0; i < detected->count; i++) {
            const obd2_detected_ecu_t *ecu = &detected->items[i];
            JsonObject obj = ecu_arr.add<JsonObject>();
            char id_str[10];
            snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)ecu->rx_id);
            obj["id"] = id_str;
            obj["active"] = active_bound && ecu->rx_id == active_rx;

            uint16_t ecu_pid_count = 0;
            for (uint16_t p = 0x01; p <= 0xFF; p++) {
                uint8_t ri = (p - 1) / 32;
                uint8_t bp = 31 - ((p - 1) % 32);
                if (ri < 8 && (ecu->supported_pids[ri] & (1UL << bp))) ecu_pid_count++;
            }
            obj["pids"] = ecu_pid_count;
        }
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_PID — cteni a dekodovani jednoho PIDu (Mode 01).
 *
 * Vola obd2_get_pid() ktery internne:
 *   1. Zkontroluje supported bitmask (jestli ECU PID podporuje)
 *   2. Odesle request na physical ECU adresu (0x7E0)
 *   3. Prijme odpoved a dekoduje ji podle tabulky (SAE J1979 Annex B)
 *
 * Odpoved obsahuje dekodovanou hodnotu, nazev PIDu a jednotku.
 *
 * Typy hodnot v odpovedi:
 *   - Bezne PIDy (teplota, otacky...): "value" jako float
 *     Priklad: {"cmd":"get_pid","pid":12,"status":"ok","name":"Engine RPM","value":875.25,"unit":"rpm"}
 *   - Dual-value PIDy (O2 senzory): "value" + "secondary"
 *     Priklad: {...,"value":0.45,"secondary":3.2,...} (napeti + proud)
 *   - Bit-encoded/enum/config/raw PIDy: "value_raw" jako hex
 *     Priklad: {...,"value_raw":"0x00070007",...}
 *
 * Hranicni pripady:
 *   - OBD neinicializovano → OBD2_ERR_NOT_INITIALIZED
 *   - PID neni podporovan ECU → OBD2_ERR_PID_NOT_SUPPORTED
 *   - ECU neodpovida (odpojeny kabel) → OBD2_ERR_TIMEOUT
 *   - ECU vraci negative response → OBD2_ERR_NEGATIVE_RESP + NRC detail
 */
void _ws_cmd_get_pid(const obd_request_msg_t *req,
                            obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_pid";
    doc["pid"] = req->pid;

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_pid_decoded_t decoded;
    obd2_status_t st = obd2_get_pid(req->pid, &decoded);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_add_pid_diag(doc["diag"].to<JsonObject>(), NULL, st);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    _ws_add_pid_diag(doc["diag"].to<JsonObject>(), &decoded, st);

    /* Sjednoceny format nazvu PIDu: "PID 0xNN - Name". Frontend tento
     * retezec zobrazuje primo (bez dalsi modifikace). Hex prefix "0x"
     * je explicitni, aby cislo nebylo zamenitelne s desitkovou soustavou. */
    char name_with_id[64];
    snprintf(name_with_id, sizeof(name_with_id), "PID 0x%02X - %s", req->pid, decoded.name);
    doc["name"] = name_with_id;
    doc["unit"] = decoded.unit;
    doc["mode"] = "01";

    /*
     * Rozliseni typu hodnoty:
     *   - Bit-encoded/enum/config/raw PIDy: presne raw data jako hex string
     *   - Dual-value PIDy (O2 senzory): primary + secondary
     *   - Ostatni: jednoducha float hodnota
     */
    const obd2_pid_desc_t *desc = obd2_get_pid_descriptor(req->pid);
    if (desc && (desc->format == OBD2_FMT_BIT_ENCODED ||
                 desc->format == OBD2_FMT_ENUM ||
                 desc->format == OBD2_FMT_CONFIG ||
                 desc->format == OBD2_FMT_RAW))
    {
        char hex[2 + OBD2_PID_MAX_DATA_BYTES * 2 + 1];
        _ws_format_pid_raw_hex(decoded, hex, sizeof(hex));
        doc["value_raw"] = hex;
    } else {
        doc["value"] = (double)decoded.value;
        /* Secondary hodnota — jen pokud neni NaN (dual-value PID) */
        if (!isnan(decoded.secondary)) {
            doc["secondary"] = (double)decoded.secondary;
        }
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_PIDS — cteni vice PIDu najednou v jednom prikazu.
 *
 * Iteruje pres pole PIDu z requestu a pro kazdy vola obd2_get_pid().
 * Odpoved obsahuje pole vysledku — kazdy s hodnotou nebo chybou.
 * Casti vysledku mohou byt uspesne i kdyz jine selzou (napr. kdyz
 * jeden PID neni podporovan ale ostatni ano).
 *
 * Maximalne WS_MAX_PIDS_PER_REQUEST PIDu (16) v jednom prikazu.
 * Pri vice PIDech roste cas zpracovani (kazdy PID = 20-50ms CAN komunikace).
 * Pro 16 PIDu je to maximalne ~800ms.
 *
 * Priklad odpovedi:
 *   {"cmd":"get_pids","results":[
 *     {"pid":12,"name":"Engine RPM","value":875.25,"unit":"rpm","status":"ok"},
 *     {"pid":5,"name":"Engine Coolant Temp","value":87,"unit":"°C","status":"ok"},
 *     {"pid":99,"status":"error","error":"PID_NOT_SUPPORTED"}
 *   ],"status":"ok"}
 *
 * Hranicni pripady:
 *   - OBD neinicializovano → cely prikaz selze (ne jednotlive PIDy)
 *   - Prazdne pole PIDu (pid_count=0) → prazdny results array, status ok
 *   - Nektery PID selze → jeho polozka v results ma status error, ostatni ok
 */
void _ws_cmd_get_pids(const obd_request_msg_t *req,
                             obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_pids";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    JsonArray results = doc["results"].to<JsonArray>();

    for (uint8_t i = 0; i < req->pid_count; i++) {
        JsonObject item = results.add<JsonObject>();
        uint8_t pid = req->pids[i];
        item["pid"] = pid;

        obd2_pid_decoded_t decoded;
        obd2_status_t st = obd2_get_pid(pid, &decoded);

        if (st == OBD2_OK) {
            item["status"] = "ok";
            _ws_add_pid_diag(item["diag"].to<JsonObject>(), &decoded, st);

            /* Sjednoceny format nazvu PIDu: "PID 0xNN - Name" — viz _ws_cmd_get_pid */
            char name_with_id[64];
            snprintf(name_with_id, sizeof(name_with_id), "PID 0x%02X - %s", pid, decoded.name);
            item["name"] = name_with_id;
            item["unit"] = decoded.unit;

            const obd2_pid_desc_t *desc = obd2_get_pid_descriptor(pid);
            if (desc && (desc->format == OBD2_FMT_BIT_ENCODED ||
                         desc->format == OBD2_FMT_ENUM ||
                         desc->format == OBD2_FMT_CONFIG ||
                         desc->format == OBD2_FMT_RAW))
            {
                char hex[2 + OBD2_PID_MAX_DATA_BYTES * 2 + 1];
                _ws_format_pid_raw_hex(decoded, hex, sizeof(hex));
                item["value_raw"] = hex;
            } else {
                item["value"] = (double)decoded.value;
                if (!isnan(decoded.secondary)) {
                    item["secondary"] = (double)decoded.secondary;
                }
            }
        } else {
            item["status"] = "error";
            item["error"]  = obd2_status_str(st);
            _ws_add_pid_diag(item["diag"].to<JsonObject>(), NULL, st);
        }
    }

    doc["status"] = "ok";
    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_SUPPORTED_PIDS — seznam podporovanych PIDu z interniho cache.
 *
 * Nevytvari CAN komunikaci — pouze cte z interniho bitmask cache
 * naplneneho pri CMD_INIT (obd2_query_supported_pids).
 * Rychla odpoved (< 1ms), vhodna pro inicializaci klientskeho UI —
 * klient muze zobrazit jen relevantni PIDy ktere ECU podporuje.
 *
 * Priklad odpovedi:
 *   {"cmd":"get_supported_pids","status":"ok","pids":[4,5,6,7,11,12,13,...],"count":28}
 *
 * Hranicni pripady:
 *   - OBD neinicializovano → chyba (cache je prazdny)
 *   - ECU nepodporuje zadne PIDy (nepravdepodobne) → prazdny array, count=0
 */
void _ws_cmd_get_supported_pids(const obd_request_msg_t *req,
                                       obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_supported_pids";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    JsonArray pids = doc["pids"].to<JsonArray>();
    for (uint16_t pid = 0x01; pid <= 0xFF; pid++) {
        if (obd2_is_pid_supported((uint8_t)pid)) {
            pids.add((uint8_t)pid);   /* cislo, ne string — viz _ws_cmd_init */
        }
    }
    doc["count"] = pids.size();
    _ws_add_active_ecu(doc);

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_DTC / CMD_GET_PENDING_DTC — cteni diagnostickych chybovych kodu.
 *
 * Zpracovava dva typy prikazu podle req->cmd:
 *   - CMD_GET_DTC (Mode 03): potvrzene chybove kody — MIL (check engine) sviti,
 *     chyba se vyskytla opakovane a splnila podminky pro potvrzeni.
 *   - CMD_GET_PENDING_DTC (Mode 07): pending kody — chyba z aktualniho
 *     nebo posledniho jezdniho cyklu, jeste nesplnila podminky pro potvrzeni.
 *     Uzitecne pro diagnostiku intermittentnich problemu.
 *
 * Pouziva broadcast (0x7DF) — vsechny ECU ve vozidle odpovi svymi DTC.
 * DTC kody jsou dekodovane do standardniho formatu:
 *   - P0xxx: Powertrain (motor, prevodovka)
 *   - C0xxx: Chassis (podvozek, ABS, ESP)
 *   - B0xxx: Body (karoserie, airbag, klimatizace)
 *   - U0xxx: Network (komunikacni chyby mezi ECU)
 *
 * Priklad odpovedi:
 *   {"cmd":"get_dtc","status":"ok","count":2,"dtcs":["P0171","P0300"]}
 *   (P0171 = chuda smes, P0300 = nahodne vypadky zapalovani)
 *
 * Hranicni pripady:
 *   - Zadne DTC → count=0, prazdny dtcs array (vozidlo je v poradku)
 *   - ECU neodpovida → TIMEOUT
 *   - Maximalne OBD2_MAX_DTC_COUNT kodu (typicky 32)
 */
void _ws_cmd_get_dtc(const obd_request_msg_t *req,
                            obd_response_msg_t *resp)
{
    JsonDocument doc;
    bool is_pending = (req->cmd == CMD_GET_PENDING_DTC);
    bool is_permanent = (req->cmd == CMD_GET_PERMANENT_DTC);
    doc["cmd"] = is_permanent ? "get_permanent_dtc"
                              : (is_pending ? "get_pending_dtc" : "get_dtc");

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_dtc_t dtcs[OBD2_MAX_DTC_COUNT];
    uint8_t count = 0;
    obd2_status_t st;

    if (is_permanent) {
        st = obd2_read_permanent_dtc(dtcs, OBD2_MAX_DTC_COUNT, &count);
    } else if (is_pending) {
        st = obd2_read_pending_dtc(dtcs, OBD2_MAX_DTC_COUNT, &count);
    } else {
        st = obd2_read_dtc(dtcs, OBD2_MAX_DTC_COUNT, &count);
    }

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    doc["count"]  = count;
    doc["mode_name"] = is_permanent ? "Mode 0A - Permanent DTCs"
                       : (is_pending ? "Mode 07 - Pending DTCs"
                                     : "Mode 03 - Stored DTCs");
    JsonArray arr = doc["dtcs"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        arr.add(dtcs[i].code);
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_CLEAR_DTC — smazani diagnostickych informaci (Mode 04).
 *
 * POZOR: Tento prikaz maze VSECHNO:
 *   - Vsechny potvrzene DTC (Mode 03)
 *   - Vsechny pending DTC (Mode 07)
 *   - Freeze frame data
 *   - Readiness bity (monitory se resetuji na "nekompletni")
 *   - MIL (check engine svetlo) se zhasne
 *
 * Podminky pro uspesne provedeni:
 *   - Zapaleni musi byt ON (klicek v pozici II)
 *   - Motor by mel byt OFF (nektere ECU to vyzaduji)
 *
 * Po smazani je potreba projet jezdni cyklus aby se monitory
 * znovu dokoncily — do te doby vozidlo neprojde emisni kontrolou (STK).
 *
 * Priklad odpovedi:
 *   {"cmd":"clear_dtc","status":"ok","message":"DTC, freeze frame a readiness bity smazany"}
 */
void _ws_cmd_get_mode06_monitor(const obd_request_msg_t *req,
                                obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_mode06_monitor";
    doc["mid"] = req->pid;

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_raw_response_t res;
    obd2_status_t st = obd2_query_raw_ex(OBD2_SID_ONBOARD_MONITOR, req->pid,
                                         &res, false);
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "Mode 06 monitor not supported by active ECU");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    char id_str[12];
    snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)res.rx_id);
    doc["rx_id"] = id_str;

    char raw_hex[OBD2_INFOTYPE_DATA_MAX * 3 + 1];
    uint16_t copy_len = res.data_len > OBD2_INFOTYPE_DATA_MAX
                            ? OBD2_INFOTYPE_DATA_MAX
                            : res.data_len;
    _ws_bytes_to_hex(res.data, copy_len, raw_hex, sizeof(raw_hex));
    doc["raw"] = raw_hex;
    doc["raw_len"] = res.data_len;
    doc["truncated"] = res.data_len > copy_len;

    if (res.data_len >= 6 &&
        res.data[0] == (OBD2_SID_ONBOARD_MONITOR + OBD2_SID_RESPONSE_OFFSET) &&
        res.data[1] == req->pid && (req->pid % 0x20) == 0) {
        JsonArray mids = doc["supported_mids"].to<JsonArray>();
        for (uint8_t byte_idx = 0; byte_idx < 4; byte_idx++) {
            uint8_t b = res.data[2 + byte_idx];
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (b & (1U << (7 - bit))) {
                    mids.add((uint8_t)(req->pid + byte_idx * 8 + bit + 1));
                }
            }
        }
    } else if (res.data_len >= 9 &&
               res.data[0] == (OBD2_SID_ONBOARD_MONITOR + OBD2_SID_RESPONSE_OFFSET) &&
               res.data[1] == req->pid) {
        JsonArray tests = doc["tests"].to<JsonArray>();
        for (uint16_t off = 2; off + 6 < res.data_len; off += 7) {
            JsonObject item = tests.add<JsonObject>();
            item["tid"] = res.data[off];
            item["value"] = ((uint16_t)res.data[off + 1] << 8) | res.data[off + 2];
            item["min"] = ((uint16_t)res.data[off + 3] << 8) | res.data[off + 4];
            item["max"] = ((uint16_t)res.data[off + 5] << 8) | res.data[off + 6];
        }
    }

    _ws_serialize(doc, resp);
}

void _ws_cmd_clear_dtc(const obd_request_msg_t *req,
                              obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "clear_dtc";

    /*
     * Autentizace — clear_dtc je destruktivni prikaz (trvale smaze DTC,
     * freeze frame a resetuje readiness monitory). Aby se omylem nespoustel
     * (napr. nahodnym klikem na tlacitko), vyzadujeme autentizacni token,
     * ktery musi klient poslat v JSON poli "token".
     *
     * Token se porovnava s hodnotou WS_AUTH_TOKEN definovanou v secrets.h.
     * Pouzivame strncmp s delkou WS_AUTH_TOKEN_MAX, aby porovnani fungovalo
     * i pri neukoncenem bufferu (pole token[] je pevne dlouhe).
     *
     * Bezpecnostni pozn.: strncmp neni konstantni v case, takze teoreticky
     * dovoluje timing attack. V kontextu tohoto projektu (lokalni WiFi AP,
     * jediny autorizovany uzivatel) je to akceptovatelne.
     */
    if (strncmp(req->token, WS_AUTH_TOKEN, WS_AUTH_TOKEN_MAX) != 0) {
        doc["status"]  = "error";
        doc["error"]   = "AUTH_INVALID";
        doc["message"] = "Chybny nebo chybejici autentizacni token";
        _ws_serialize(doc, resp);
        return;
    }

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_status_t st = obd2_clear_dtc();
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"]  = "ok";
    doc["message"] = "DTC, freeze frame a readiness bity smazany";
    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_VIN — cteni identifikacniho cisla vozidla (Mode 09, InfoType $02).
 *
 * VIN je 17-znakovy retezec podle ISO 3779 / ISO 3780.
 * Obsahuje informace o vyrobci, modelu, roku vyroby a seriove cislo.
 * Priklad: "WVWZZZ3CZWE123456"
 *   - WVW = Volkswagen
 *   - ZZZ = vyplnove znaky
 *   - 3C = Passat
 *   - Z = kontrolni cislo
 *   - W = rok 2024
 *   - E = zavod
 *   - 123456 = seriove cislo
 *
 * Nektere ECU (napr. Peugeot, Citroen s EOBD) VIN pres Mode 09
 * nepodporuji — ECU vrati negative response (NRC $12 subFunctionNotSupported).
 * To je normalni chovani a klient zobrazi prislusnou chybu.
 *
 * Priklad odpovedi:
 *   {"cmd":"get_vin","status":"ok","vin":"WVWZZZ3CZWE123456"}
 *
 * Hranicni pripady:
 *   - ECU nepodporuje Mode 09 → NEGATIVE_RESP
 *   - Multi-frame odpoved (VIN ma 17 znaku = vyzaduje ISO-TP segmentaci)
 */
void _ws_cmd_get_vin(const obd_request_msg_t *req,
                            obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_vin";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    /* Multi-ECU broadcast — nasbira VIN od vsech jednotek, ktere odpovedi. */
    obd2_vin_list_t list;
    obd2_status_t st = obd2_read_vin_all(&list);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "VIN neni podporovano zadnou ECU v siti");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";

    /* Zpetna kompatibilita: "vin" = prvni odpovedi ECU (typicky jedina).
     * Pokud je ECU vic, prida se pole "vins" s rx_id + VIN per ECU. */
    doc["vin"] = list.items[0].vin;
    if (list.count > 1) {
        JsonArray arr = doc["vins"].to<JsonArray>();
        for (uint8_t i = 0; i < list.count; i++) {
            JsonObject item = arr.add<JsonObject>();
            char id_str[10];
            snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)list.items[i].rx_id);
            item["id"]  = id_str;
            item["vin"] = list.items[i].vin;
        }
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_MONITOR_STATUS — emisni readiness stav (PID $01, Mode 01).
 *
 * Dekoduje 4-bytovou odpoved PID $01 na strukturovany JSON:
 *   - MIL (Malfunction Indicator Lamp = check engine svetlo): zapnuto/vypnuto
 *   - Pocet ulozenych DTC
 *   - Stav kazdého emisniho monitoru: supported (ECU ho ma) + ready (test dokoncen)
 *
 * Tato informace je klicova pro emisni kontrolu (STK/TUV/MOT):
 *   - Pokud je MIL zapnuta → vozidlo neprojde
 *   - Pokud nektery podporovany monitor neni ready → muze neprojit
 *     (zavisí na pravidlech dane zeme)
 *
 * Monitory se deli na:
 *   Kontinualni (bezi stale pri chodu motoru):
 *     - misfire: vypadky zapalovani
 *     - fuel_system: regulace paliva (lambda)
 *     - components: kontrola komponent (senzory, aktuatory)
 *
 *   Nekontinualni (bezi jen za specifickych podminek):
 *     - catalyst: katalyzator (vyzaduje ustalenou jizdu)
 *     - heated_cat: vyhrivany katalyzator
 *     - evap_system: tesnost paliove soustavy (EVAP)
 *     - secondary_air: sekundarni vzduch
 *     - ac_refrigerant: klimatizacni chladivo
 *     - oxygen_sensor: lambda sonda
 *     - oxygen_heater: ohrev lambda sondy
 *     - egr_system: recirkulace vyfukovych plynu (EGR)
 *
 * Priklad odpovedi:
 *   {"cmd":"get_monitor_status","status":"ok","mil":false,"dtc_count":0,
 *    "monitors":{"misfire":{"sup":true,"rdy":true},"catalyst":{"sup":true,"rdy":false},...}}
 */
/**
 * @brief Pomocna funkce — naplni JsonObject readiness monitory pro jednu ECU.
 *
 * Sdilena mezi _ws_cmd_get_monitor_status() a _ws_cmd_get_monitor_status_all().
 */
static void _ws_add_monitor_to_json(JsonObject &mon, const obd2_monitor_status_t &status)
{
    JsonObject m;
    bool is_diesel = status.is_compression;

    /* Kontinualni monitory (bezi stale) */
    m = mon["misfire"].to<JsonObject>();
    m["name"] = "Misfire"; m["sup"] = status.misfire_sup;  m["rdy"] = status.misfire_rdy;

    m = mon["fuel_system"].to<JsonObject>();
    m["name"] = "Fuel System"; m["sup"] = status.fuel_sys_sup; m["rdy"] = status.fuel_sys_rdy;

    m = mon["components"].to<JsonObject>();
    m["name"] = "Components"; m["sup"] = status.ccm_sup;      m["rdy"] = status.ccm_rdy;

    /* Nekontinualni monitory — dynamicke popisky podle typu motoru */
    m = mon["catalyst"].to<JsonObject>();
    m["name"] = is_diesel ? "NMHC Catalyst" : "Catalyst";
    m["sup"] = status.cat_sup;      m["rdy"] = status.cat_rdy;

    m = mon["heated_cat"].to<JsonObject>();
    m["name"] = is_diesel ? "NOx/SCR Monitor" : "Heated Catalyst";
    m["sup"] = status.hcat_sup;     m["rdy"] = status.hcat_rdy;

    m = mon["evap_system"].to<JsonObject>();
    m["name"] = is_diesel ? "Boost Pressure" : "Evaporative System";
    m["sup"] = is_diesel ? status.air_sup : status.evap_sup;
    m["rdy"] = is_diesel ? status.air_rdy : status.evap_rdy;

    m = mon["secondary_air"].to<JsonObject>();
    m["name"] = is_diesel ? "Exhaust Gas Sensor" : "Secondary Air";
    m["sup"] = is_diesel ? status.o2s_sup : status.air_sup;
    m["rdy"] = is_diesel ? status.o2s_rdy : status.air_rdy;

    m = mon["ac_refrigerant"].to<JsonObject>();
    m["name"] = is_diesel ? "Reserved" : "A/C Refrigerant";
    m["sup"] = is_diesel ? false : status.acrf_sup;
    m["rdy"] = is_diesel ? false : status.acrf_rdy;

    m = mon["oxygen_sensor"].to<JsonObject>();
    m["name"] = is_diesel ? "PM Filter Monitor" : "Oxygen Sensor";
    m["sup"] = is_diesel ? status.htr_sup : status.o2s_sup;
    m["rdy"] = is_diesel ? status.htr_rdy : status.o2s_rdy;

    m = mon["oxygen_heater"].to<JsonObject>();
    m["name"] = is_diesel ? "Reserved" : "Oxygen Sensor Heater";
    m["sup"] = is_diesel ? false : status.htr_sup;
    m["rdy"] = is_diesel ? false : status.htr_rdy;

    m = mon["egr_system"].to<JsonObject>();
    m["name"] = is_diesel ? "EGR and/or VVT" : "EGR System";
    m["sup"] = status.egr_sup;      m["rdy"] = status.egr_rdy;
}

static void _ws_add_monitor_diag(JsonObject obj, const obd2_monitor_status_t &status)
{
    char raw_hex[16];
    snprintf(raw_hex, sizeof(raw_hex), "%02X %02X %02X %02X",
             status.raw[0], status.raw[1], status.raw[2], status.raw[3]);
    obj["ignition"] = status.is_compression ? "compression" : "spark";
    obj["raw"] = raw_hex;
}

void _ws_cmd_get_monitor_status(const obd_request_msg_t *req,
                                       obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_monitor_status";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_monitor_status_t status;
    obd2_status_t st = obd2_get_monitor_status(NULL, &status);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"]    = "ok";
    doc["mil"]       = status.mil_on;
    doc["dtc_count"] = status.dtc_count;
    JsonObject root = doc.as<JsonObject>();
    _ws_add_monitor_diag(root, status);

    JsonObject mon = doc["monitors"].to<JsonObject>();
    _ws_add_monitor_to_json(mon, status);

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_MONITOR_STATUS_ALL — hromadne cteni readiness monitoru
 * ze vsech ECU pres broadcast (Mode 01 PID 0x01).
 *
 * Hlavni use-case: emisni kontrola (overit ze VSECHNY emisne relevantni
 * ECU jsou "ready"). Vraci per-ECU pole s rx_id + monitor objekt.
 *
 * Priklad odpovedi:
 *   {"cmd":"get_monitor_status_all","status":"ok",
 *    "ecus":[{"id":"0x7E8","mil":false,"dtc_count":0,"monitors":{...}},
 *            {"id":"0x7E9","mil":false,"dtc_count":0,"monitors":{...}}]}
 */
void _ws_cmd_get_monitor_status_all(const obd_request_msg_t *req,
                                            obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_monitor_status_all";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_monitor_status_list_t list;
    obd2_status_t st = obd2_get_monitor_status_all(&list);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    JsonArray ecus = doc["ecus"].to<JsonArray>();

    for (uint8_t i = 0; i < list.count; i++) {
        JsonObject obj = ecus.add<JsonObject>();
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)list.items[i].rx_id);
        obj["id"]        = id_str;
        obj["mil"]       = list.items[i].status.mil_on;
        obj["dtc_count"] = list.items[i].status.dtc_count;
        _ws_add_monitor_diag(obj, list.items[i].status);
        JsonObject mon = obj["monitors"].to<JsonObject>();
        _ws_add_monitor_to_json(mon, list.items[i].status);
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_FREEZE_FRAME — cteni freeze frame PIDu (Mode 02).
 *
 * Freeze frame uchovava "snimek" hodnot PIDu v okamziku vzniku DTC.
 * Uzitecne pro diagnostiku — napr. jake byly otacky, teplota a zatez
 * motoru kdyz se vyskytla chyba.
 *
 * Request obsahuje PID (který je požadováno z freeze frame přečíst)
 * a cislo freeze frame (hardcoded 0x00 — prvni/jediny frame).
 * Vetsina ECU podporuje pouze frame 0x00.
 *
 * Odpoved obsahuje:
 *   - Dekodovanou hodnotu (stejne jako u Mode 01)
 *   - Raw data jako hex string pro pokrocilou diagnostiku
 *   - Sekundarni hodnotu pokud existuje (dual-value PIDy)
 *
 * Priklad odpovedi:
 *   {"cmd":"get_freeze_frame","pid":12,"status":"ok","data_len":2,
 *    "value":2500.0,"name":"Engine RPM","unit":"rpm","raw":"09C4"}
 *
 * Hranicni pripady:
 *   - Zadne DTC → freeze frame muze byt prazdny (ECU vrati NEGATIVE_RESP)
 *   - PID neni v freeze frame → ECU vrati chybu
 *   - Dekodovani selze (neznamy PID) → val je NaN, odpoved bude bez "value"
 */
void _ws_cmd_get_freeze_frame(const obd_request_msg_t *req,
                                      obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_freeze_frame";
    doc["pid"] = req->pid;

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_freeze_frame_raw(req->pid, 0x00, &raw);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, NULL);
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"]   = "ok";
    doc["data_len"] = raw.data_len;

    /* Hodnota se dekóduje stejným způsobem jako Mode 01 */
    float val = obd2_decode_pid_value(req->pid, raw.data, raw.data_len);
    if (!isnan(val)) {
        const obd2_pid_desc_t *desc = obd2_get_pid_descriptor(req->pid);
        doc["value"] = (double)val;
        if (desc) {
            doc["name"] = desc->name;
            doc["unit"] = desc->unit;
        }
        float sec = obd2_decode_pid_secondary(req->pid, raw.data, raw.data_len);
        if (!isnan(sec)) {
            doc["secondary"] = (double)sec;
        }
    }

    /* Raw data jako hex string pro pokrocilou diagnostiku */
    char hex[12];
    int off = 0;
    for (uint8_t i = 0; i < raw.data_len && off < 10; i++) {
        off += snprintf(hex + off, sizeof(hex) - off, "%02X", raw.data[i]);
    }
    doc["raw"] = hex;

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_ECU_NAME — cteni jmena ridici jednotky (Mode 09, InfoType $0A).
 *
 * Vraci textovy identifikator ECU — typicky 4-znakova zkratka
 * nasledovana popisem. Format zavisi na vyrobci.
 * Priklad: "ECM\0-Engine Control Module" nebo "TCM\0-Transmission"
 *
 * Nektere ECU (zejmena starsi nebo levnejsi) tuto funkci nepodporuji
 * a vrati negative response — to je normalni a ocekavane chovani.
 *
 * Priklad odpovedi:
 *   {"cmd":"get_ecu_name","status":"ok","ecu_name":"ECM-Engine Control Module"}
 *
 * Hranicni pripady:
 *   - ECU nepodporuje InfoType $0A → NEGATIVE_RESP
 *   - Jmeno obsahuje non-printable znaky → mohou se zobrazit v JSON
 *   - Maximalni delka: OBD2_ECU_NAME_MAX_LENGTH znaku
 */
void _ws_cmd_get_ecu_name(const obd_request_msg_t *req,
                                  obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_ecu_name";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    /* Multi-ECU broadcast — posbira jmena od vsech odpovedajicich ECU. */
    obd2_ecu_name_list_t list;
    obd2_status_t st = obd2_read_ecu_names_all(&list);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "ECU name not supported by any ECU");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"]   = "ok";
    doc["ecu_name"] = list.items[0].name;  /* zpetna kompatibilita */
    if (list.count > 1) {
        JsonArray arr = doc["ecu_names"].to<JsonArray>();
        for (uint8_t i = 0; i < list.count; i++) {
            JsonObject item = arr.add<JsonObject>();
            char id_str[10];
            snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)list.items[i].rx_id);
            item["id"]   = id_str;
            item["name"] = list.items[i].name;
        }
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_DISCOVER_ECUS — detekce vsech ECU v siti.
 *
 * Primarni zdroj: ECU detekovane pri broadcast PID $00 behem init
 * (obd2_query_supported_pids). Kazda ECU, ktera odpovi na povinny
 * PID $00, je zaznamenana vcetne per-ECU bitmasky podporovanych PIDu.
 *
 * Sekundarni doplneni: pro kazdou nalezenou ECU se pokusi nacist
 * lidsky citelny nazev pres Mode 09 InfoType $0A (broadcast).
 * Pokud ECU $0A nepodporuje (napr. prevodovka), zobrazi se pouze
 * CAN ID bez nazvu — to je bezne a ocekavane.
 *
 * Predchozi implementace pouzivala vyhradne Mode 09 $0A pro detekci,
 * coz zpusobovalo, ze ECU nepodporujici $0A (napr. 0x7E9 prevodovka)
 * nebyly detekovany vubec.
 *
 * Priklad odpovedi:
 *   {"cmd":"discover_ecus","status":"ok",
 *    "ecus":[{"id":"0x7E8","name":"ECM-EngineControl","pids":19},
 *            {"id":"0x7E9","name":"","pids":2}]}
 */
void _ws_cmd_discover_ecus(const obd_request_msg_t *req,
                                   obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "discover_ecus";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    /* Primarni zdroj: ECU z init (broadcast PID $00) */
    const obd2_detected_ecu_list_t *detected = obd2_get_detected_ecus();

    if (detected->count == 0) {
        _ws_set_error(doc, OBD2_ERR_NO_DATA, "No ECU detected during init");
        _ws_serialize(doc, resp);
        return;
    }

    /* Sekundarni: pokus o doplneni nazvu pres Mode 09 $0A (broadcast).
     * Selhani je OK — ne vsechny ECU InfoType $0A podporuji. */
    obd2_ecu_name_list_t name_list;
    memset(&name_list, 0, sizeof(name_list));
    obd2_read_ecu_names_all(&name_list);  /* vysledek ignorujeme pri chybe */

    doc["status"] = "ok";
    _ws_add_active_ecu(doc);
    JsonArray ecus = doc["ecus"].to<JsonArray>();
    uint32_t active_tx = 0;
    uint32_t active_rx = 0;
    bool active_bound = obd2_get_active_ecu(&active_tx, &active_rx);

    for (uint8_t i = 0; i < detected->count; i++) {
        const obd2_detected_ecu_t *ecu = &detected->items[i];
        JsonObject obj = ecus.add<JsonObject>();

        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)ecu->rx_id);
        obj["id"] = id_str;
        obj["active"] = active_bound && ecu->rx_id == active_rx;

        /* Spocitani PIDu pro tuto ECU */
        uint16_t pid_count = 0;
        for (uint16_t pid = 0x01; pid <= 0xFF; pid++) {
            uint8_t ri = (pid - 1) / 32;
            uint8_t bp = 31 - ((pid - 1) % 32);
            if (ri < 8 && (ecu->supported_pids[ri] & (1UL << bp))) pid_count++;
        }
        obj["pids"] = pid_count;

        /* Doplneni nazvu pokud Mode 09 $0A vratil shodu pro toto rx_id */
        const char *name = "";
        for (uint8_t j = 0; j < name_list.count; j++) {
            if (name_list.items[j].rx_id == ecu->rx_id) {
                name = name_list.items[j].name;
                break;
            }
        }
        obj["name"] = name;
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_GET_CAL_ID — cteni kalibracnich identifikatoru (Mode 09, InfoType $04).
 *
 * Kazde CalID je 16-znakovy retezec identifikujici verzi softwarove
 * kalibrace ECU. Muze byt vice nez jedno — NODI byte v odpovedi
 * urcuje pocet (typicky 1-4).
 *
 * CalID je dulezite pro emisni kontrolu — overuje se ze ECU
 * pouziva schvalenou verzi software (ne tuning/chip-tuning).
 *
 * Priklad odpovedi:
 *   {"cmd":"get_cal_id","status":"ok","count":2,
 *    "cal_ids":["39101-03CA3   ","39101-03CB1   "]}
 *
 * Hranicni pripady:
 *   - ECU nepodporuje InfoType $04 → NEGATIVE_RESP
 *   - CalID muze obsahovat mezery (padding do 16 znaku)
 *   - Maximalne OBD2_MAX_INFO_ITEMS polozek
 */
void _ws_cmd_get_cal_id(const obd_request_msg_t *req,
                                obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_cal_id";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    /* Multi-ECU broadcast — posbira CalID od vsech odpovedajicich ECU. */
    obd2_calid_list_t list;
    obd2_status_t st = obd2_read_calids_all(&list);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "Calibration ID not supported by any ECU");
        _ws_serialize(doc, resp);
        return;
    }

    /* Zpetna kompatibilita: "count" + "cal_ids" od PRVNI ECU. */
    doc["status"] = "ok";
    doc["count"]  = list.items[0].count;
    JsonArray arr = doc["cal_ids"].to<JsonArray>();
    for (uint8_t i = 0; i < list.items[0].count; i++) {
        arr.add(list.items[0].cal_ids[i]);
    }

    /* Pokud odpovedelo vic ECU, přidá se rozsirene per-ECU pole. */
    if (list.count > 1) {
        JsonArray eca = doc["ecu_cal_ids"].to<JsonArray>();
        for (uint8_t i = 0; i < list.count; i++) {
            JsonObject item = eca.add<JsonObject>();
            char id_str[10];
            snprintf(id_str, sizeof(id_str), "0x%03X", (unsigned)list.items[i].rx_id);
            item["id"] = id_str;
            JsonArray cids = item["cal_ids"].to<JsonArray>();
            for (uint8_t j = 0; j < list.items[i].count; j++) {
                cids.add(list.items[i].cal_ids[j]);
            }
        }
    }

    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_START_STREAM — spusteni periodickeho cteni PIDu.
 *
 * Klient posle rezim streamu, seznam PIDu a volitelny interval (vychozi 200ms).
 * OBD task pak cyklicky cte tyto PIDy a broadcastuje vysledky
 * vsem pripojenym klientum. DASH rezim zachovava kompaktní format pro UI,
 * Inspector rezim pridava diagnostiku raw/ECU/status pro vybrane PIDy.
 *
 * Konfigurace se zapisuje do sdilene volatile struktury _stream_cfg.
 * OBD task ji precte v dalsim cyklu. Poradi zapisu je dulezite:
 * nejdriv pids[], pid_count a interval_ms, az nakonec active=true.
 * Tim se minimalizuje riziko cteni nekonzistentni konfigurace.
 *
 * Pokud uz stream bezi, tento prikaz prepise konfiguraci
 * (zmena PIDu/intervalu za behu bez nutnosti stop_stream).
 *
 * Priklad requestu:
 *   {"cmd":"start_stream","mode":"dash","pids":[12,13,5],"interval_ms":100}
 *   {"cmd":"start_stream","mode":"inspector","pids":[17],"diag_pids":[17],"interval_ms":500}
 *
 * Priklad odpovedi:
 *   {"cmd":"start_stream","status":"ok","mode":"dash","pid_count":3,"interval_ms":100}
 *
 * Hranicni pripady:
 *   - OBD neinicializovano → chyba
 *   - Prazdne pole PIDu (pid_count=0) → chyba NO_PIDS
 *   - Interval < 50ms → automaticky nastaven na 200ms (ochrana pred pretizenim)
 *   - Interval >= 50ms → pouzit tak jak je
 *   - Maximalne WS_MAX_PIDS_PER_REQUEST (16) PIDu v DASH rezimu
 *   - Inspector request je parserem omezen na WS_MAX_DIAG_PIDS (4) PIDy
 */
void _ws_cmd_get_supported_infotypes(const obd_request_msg_t *req,
                                     obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_supported_infotypes";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_infotype_list_t list;
    obd2_status_t st = obd2_read_infotype_all(OBD2_INFOTYPE_SUPPORTED, &list);
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "Mode 09 InfoType 00 not supported by any ECU");
        _ws_serialize(doc, resp);
        return;
    }

    bool seen[256] = {false};
    doc["status"] = "ok";
    JsonArray union_arr = doc["infotypes"].to<JsonArray>();
    JsonArray ecus = doc["ecus"].to<JsonArray>();

    for (uint8_t i = 0; i < list.count; i++) {
        const obd2_infotype_item_t *src = &list.items[i];
        JsonObject item = ecus.add<JsonObject>();
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)src->rx_id);
        item["id"] = id_str;

        char raw_hex[OBD2_INFOTYPE_DATA_MAX * 3 + 1];
        _ws_bytes_to_hex(src->data, src->data_len, raw_hex, sizeof(raw_hex));
        item["raw"] = raw_hex;
        item["raw_len"] = src->data_len;

        JsonArray supported = item["infotypes"].to<JsonArray>();
        for (uint16_t byte_idx = 0; byte_idx < src->data_len; byte_idx++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (src->data[byte_idx] & (1U << (7 - bit))) {
                    uint8_t infotype = (uint8_t)(byte_idx * 8 + bit + 1);
                    supported.add(infotype);
                    if (!seen[infotype]) {
                        seen[infotype] = true;
                        union_arr.add(infotype);
                    }
                }
            }
        }
    }
    doc["count"] = union_arr.size();

    _ws_serialize(doc, resp);
}

void _ws_cmd_get_mode09_info(const obd_request_msg_t *req,
                             obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_mode09_info";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    uint8_t infotype = req->pid;
    obd2_infotype_list_t list;
    obd2_status_t st = obd2_read_infotype_all(infotype, &list);
    doc["infotype"] = infotype;

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "Mode 09 InfoType not supported by any ECU");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    doc["count"] = list.count;
    JsonArray ecus = doc["ecus"].to<JsonArray>();

    for (uint8_t i = 0; i < list.count; i++) {
        const obd2_infotype_item_t *src = &list.items[i];
        JsonObject item = ecus.add<JsonObject>();
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)src->rx_id);
        item["id"] = id_str;
        item["nodi"] = src->nodi;
        item["raw_len"] = src->data_len;
        item["truncated"] = src->truncated;

        char raw_hex[OBD2_INFOTYPE_DATA_MAX * 3 + 1];
        _ws_bytes_to_hex(src->data, src->data_len, raw_hex, sizeof(raw_hex));
        item["raw"] = raw_hex;
    }

    _ws_serialize(doc, resp);
}

void _ws_cmd_get_cvn(const obd_request_msg_t *req,
                     obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_cvn";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    obd2_cvn_list_t list;
    obd2_status_t st = obd2_read_cvns_all(&list);
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "CVN not supported by any ECU");
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    doc["count"] = list.items[0].count;
    JsonArray cvns = doc["cvns"].to<JsonArray>();
    for (uint8_t i = 0; i < list.items[0].count; i++) {
        char cvn_str[12];
        snprintf(cvn_str, sizeof(cvn_str), "%08lX",
                 (unsigned long)list.items[0].cvns[i]);
        cvns.add(cvn_str);
    }

    JsonArray ecus = doc["ecu_cvns"].to<JsonArray>();
    for (uint8_t i = 0; i < list.count; i++) {
        JsonObject item = ecus.add<JsonObject>();
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)list.items[i].rx_id);
        item["id"] = id_str;
        JsonArray arr = item["cvns"].to<JsonArray>();
        for (uint8_t j = 0; j < list.items[i].count; j++) {
            char cvn_str[12];
            snprintf(cvn_str, sizeof(cvn_str), "%08lX",
                     (unsigned long)list.items[i].cvns[j]);
            arr.add(cvn_str);
        }
    }

    _ws_serialize(doc, resp);
}

void _ws_cmd_get_ipt(const obd_request_msg_t *req,
                     obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "get_ipt";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Call init first");
        _ws_serialize(doc, resp);
        return;
    }

    uint8_t infotype = (req->pid == OBD2_INFOTYPE_IPT_COMPRESSION)
                           ? OBD2_INFOTYPE_IPT_COMPRESSION
                           : OBD2_INFOTYPE_IPT;

    obd2_infotype_list_t list;
    obd2_status_t st = obd2_read_infotype_all(infotype, &list);
    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "IPT not supported by any ECU for requested InfoType");
        doc["infotype"] = infotype;
        _ws_serialize(doc, resp);
        return;
    }

    doc["status"] = "ok";
    doc["infotype"] = infotype;
    doc["kind"] = (infotype == OBD2_INFOTYPE_IPT_COMPRESSION) ? "compression" : "spark";
    doc["count"] = list.count;
    JsonArray ecus = doc["ecus"].to<JsonArray>();

    for (uint8_t i = 0; i < list.count; i++) {
        const obd2_infotype_item_t *src = &list.items[i];
        JsonObject item = ecus.add<JsonObject>();
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "0x%03lX", (unsigned long)src->rx_id);
        item["id"] = id_str;
        item["nodi"] = src->nodi;
        item["raw_len"] = src->data_len;
        item["truncated"] = src->truncated;

        char raw_hex[OBD2_INFOTYPE_DATA_MAX * 3 + 1];
        _ws_bytes_to_hex(src->data, src->data_len, raw_hex, sizeof(raw_hex));
        item["raw"] = raw_hex;

        JsonArray counters = item["counters"].to<JsonArray>();
        uint8_t idx = 0;
        for (uint16_t off = 0; off + 1 < src->data_len; off += 2, idx++) {
            JsonObject c = counters.add<JsonObject>();
            uint16_t value = ((uint16_t)src->data[off] << 8) | src->data[off + 1];
            c["idx"] = idx;
            c["name"] = _ws_ipt_counter_name(infotype, idx);
            c["value"] = value;
        }
    }

    _ws_serialize(doc, resp);
}

void _ws_cmd_start_stream(const obd_request_msg_t *req,
                                  obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "start_stream";

    if (!_obd_initialized) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Zavolejte nejdrive init");
        _ws_serialize(doc, resp);
        return;
    }

    if (req->pid_count == 0) {
        doc["status"]  = "error";
        doc["error"]   = "NO_PIDS";
        doc["message"] = "Zadejte alespon jeden PID ke streamovani";
        _ws_serialize(doc, resp);
        return;
    }

    /* Nastaveni stream konfigurace — OBD task ji precte v dalsim cyklu */
    _stream_cfg.active = false;
    _stream_cfg.mode = req->stream_mode;
    for (uint8_t i = 0; i < req->pid_count; i++) {
        _stream_cfg.pids[i] = req->pids[i];
    }
    _stream_cfg.pid_count   = req->pid_count;
    _stream_cfg.diag_pid_count = req->diag_pid_count;
    for (uint8_t i = 0; i < req->diag_pid_count && i < WS_MAX_DIAG_PIDS; i++) {
        _stream_cfg.diag_pids[i] = req->diag_pids[i];
    }
    if (_stream_cfg.mode == WS_STREAM_MODE_INSPECTOR &&
        _stream_cfg.diag_pid_count == 0) {
        uint8_t copy_count = req->pid_count < WS_MAX_DIAG_PIDS
                           ? req->pid_count
                           : WS_MAX_DIAG_PIDS;
        for (uint8_t i = 0; i < copy_count; i++) {
            _stream_cfg.diag_pids[i] = req->pids[i];
        }
        _stream_cfg.diag_pid_count = copy_count;
    }
    _stream_cfg.interval_ms = (req->interval_ms >= 50) ? req->interval_ms : 200;
    _stream_cfg.active      = true;  /* Zapnout az po nastaveni parametru */

    doc["status"]      = "ok";
    doc["mode"]        = _ws_stream_mode_str(_stream_cfg.mode);
    doc["pid_count"]   = req->pid_count;
    doc["interval_ms"] = _stream_cfg.interval_ms;
    JsonArray pids = doc["pids"].to<JsonArray>();
    for (uint8_t i = 0; i < req->pid_count; i++) {
        pids.add(req->pids[i]);
    }
    JsonArray diag_pids = doc["diag_pids"].to<JsonArray>();
    for (uint8_t i = 0; i < _stream_cfg.diag_pid_count && i < WS_MAX_DIAG_PIDS; i++) {
        diag_pids.add((uint8_t)_stream_cfg.diag_pids[i]);
    }
    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_STOP_STREAM — zastaveni periodickeho cteni PIDu.
 *
 * Nastavi _stream_cfg.active na false. OBD task se pak vrati
 * do rezimu blokujiciho cekani na jednotlive prikazy z fronty
 * (xQueueReceive s portMAX_DELAY).
 *
 * Konfigurace (pids, interval) zustava zachovana — dalsi
 * start_stream bez parametru by mohl teoreticky pokracovat
 * (ale aktualne se parametry vzdy prepisuji).
 *
 * Priklad odpovedi:
 *   {"cmd":"stop_stream","status":"ok"}
 *
 * Hranicni pripady:
 *   - Volano kdyz stream nebezi → nevadi, active uz je false, odpoved ok
 *   - Nevyzaduje OBD inicializaci (jen nastavi flag)
 */
void _ws_cmd_stop_stream(const obd_request_msg_t *req,
                                 obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "stop_stream";

    _stream_cfg.active = false;
    _stream_cfg.diag_pid_count = 0;
    _stream_cfg.mode = WS_STREAM_MODE_DASH;

    doc["status"] = "ok";
    doc["mode"] = "idle";
    _ws_serialize(doc, resp);
}

/**
 * @brief CMD_MANUAL_QUERY — provedení nízkoúrovňového OBD dotazu.
 * 
 * Slouží pro diagnostický terminál. Umožňuje manuální zadání Service a PID
 * a vrací surovou HEX odpověď včetně CAN ID.
 *
 * Implementuje bezpečnostní whitelist (Services 01, 02, 03, 07, 09), aby nebylo
 * možné omylem vyslat zápisové nebo nebezpečné diagnostické služby.
 *
 * Příklad odpovědi:
 *   {"cmd":"manual_query","status":"ok","rx_id":"0x7E8","payload":"41 0C 0F A0","interp_val":1000.0}
 */
void _ws_cmd_manual_query(const obd_request_msg_t *req,
                                  obd_response_msg_t *resp)
{
    JsonDocument doc;
    doc["cmd"] = "manual_query";

    uint8_t service = req->service;
    uint8_t pid     = req->pid;
    const bool preinit_pid00_probe = (!_obd_initialized &&
                                      service == OBD2_SID_CURRENT_DATA &&
                                      pid == 0x00);

    if (!_obd_initialized && !preinit_pid00_probe) {
        _ws_set_error(doc, OBD2_ERR_NOT_INITIALIZED, "Init OBD first!");
        _ws_serialize(doc, resp);
        return;
    }

    /* Safety Whitelist: Povolíme jen bezpečné čtecí služby */
    bool safe = false;
    if (service == 0x01 || service == 0x02 || service == 0x03 ||
        service == 0x06 || service == 0x07 || service == 0x09 ||
        service == 0x0A) {
        safe = true;
    }

    if (!safe) {
        _ws_set_error(doc, OBD2_ERR_INVALID_ARG, "Security violation: Service blocked.");
        _ws_serialize(doc, resp);
        return;
    }

    if (preinit_pid00_probe) {
        obd2_status_t init_st = _ws_start_obd_transport();
        if (init_st != OBD2_OK) {
            _ws_set_error(doc, init_st, "transport init failed");
            _ws_add_init_diag(doc);
            _ws_serialize(doc, resp);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    obd2_raw_response_t res;
    /* Pokud delame preinit_pid00_probe, vynutime broadcast, abychom probudili spici BSI / ECU */
    obd2_status_t st = obd2_query_raw_ex(service, pid, &res, preinit_pid00_probe);

    if (st != OBD2_OK) {
        _ws_set_error(doc, st, "Communication failed");
        if (preinit_pid00_probe) {
            _ws_add_init_diag(doc);
        }
    } else {
        doc["status"] = "ok";
        if (preinit_pid00_probe) {
            doc["transport_only"] = true;
            _ws_add_init_diag(doc);
        }
        char id_str[12];
        sprintf(id_str, "0x%03X", res.rx_id);
        doc["rx_id"] = id_str;
        doc["svc"]   = res.service;
        doc["pid"]   = res.pid;
        doc["is_neg"] = res.is_negative;
        
        if (res.is_negative) {
            doc["nrc"] = res.nrc_code;
        }

        /* Formátování dat jako HEX string — bezpečné zvětšení bufferu.
         * Max payload je 256 B, každý byte zabere 3 znaky ("XX ") + terminátor.
         * 256 * 3 + 1 = 769, 1024 je bezpečná rezerva. */
        char hex[1024] = "";
        char tmp[4];
        int pos = 0;
        for (int i = 0; i < res.data_len; i++) {
            int written = snprintf(tmp, sizeof(tmp), "%02X ", res.data[i]);
            if (pos + written < (int)sizeof(hex)) {
                strcat(hex, tmp);
                pos += written;
            }
        }
        if (pos > 0) {
            hex[pos - 1] = '\0'; // Odstranění poslední mezery
        }
        doc["payload"] = hex;

        /* Pokud kód známe, zkusíme přidat interpretaci */
        const obd2_pid_desc_t *desc = obd2_get_pid_descriptor(res.pid);
        if (desc && res.service == 0x01 && !res.is_negative) {
            doc["interp_name"] = desc->name;
            /* Pro Mode 01 je offset dat 2 (Byte 0 = SID+40, Byte 1 = PID) */
            if (res.data_len >= 3) {
                float val = obd2_decode_pid_value(res.pid, &res.data[2], res.data_len - 2);
                if (!isnan(val)) {
                    doc["interp_val"]  = val;
                    doc["interp_unit"] = desc->unit;
                }
            }
        }
    }

    _ws_serialize(doc, resp);
}
