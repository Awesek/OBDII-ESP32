/**
 * @file test_obd2.c
 * @brief Integracni testy OBD-II vrstvy nad mockovanou ISO-TP/TWAI.
 *
 * Na rozdil od test_obd2_pids.c a test_obd2_diag.c (ciste dekodery),
 * tyto testy zkousi cely retezec:
 *   1) obd2_xxx funkce volaji _obd2_request
 *   2) _obd2_request volaji isotp_transaction / isotp_transaction_broadcast
 *   3) isotp vrstva posila CAN ramce pres twai_transmit (zachyceno v mocku)
 *   4) mock dodava odpovedi ECU pres RX frontu
 *
 * Pokryti:
 *   - obd2_get_pid_raw: unicast SF happy path, PID echo mismatch, negativni
 *                        odpoved (0x7F + NRC capture), timeout, NULL result,
 *                        not-initialized guard
 *   - obd2_get_pid:     dekoduje hodnotu a plni name/unit z tabulky
 *   - obd2_read_dtc:    0 DTC (broadcast SF), 2 DTC (broadcast SF),
 *                        5 DTC (broadcast FF+CF), NULL kontrola
 *   - obd2_clear_dtc:   uspech, NRC odpoved (0x22 conditionsNotCorrect)
 *   - obd2_read_vin:    multi-frame FF+CF+CF, maly buffer
 *   - obd2_read_ecu_name: uspech
 *
 * Klicova idea: mock TWAI dodava ramce presne tak, jak by prisly z realne ECU.
 * Timeouty jsou simulovane (mock posouva cas pri prazdne RX fronte), takze
 * testy bezi deterministicky a instantne.
 */

#include <stdio.h>
#include <string.h>
#include "unity_lite.h"
#include "mock_twai.h"
#include "obd2.h"
#include "isotp.h"

/* ========================================================================= */
/*  Pomocne funkce                                                           */
/* ========================================================================= */

/**
 * Inicializuje OBD-II vrstvu pred kazdym testem.
 * Tise vypne logy (jinak by vystup testu byl zasypan pouze ladicimi zpravami).
 */
static void setup_obd2(void)
{
    mock_twai_reset();
    isotp_set_log_level(ISOTP_LOG_NONE);
    obd2_set_log_level(ISOTP_LOG_NONE);
    obd2_status_t s = obd2_init(500000, 5, 4);
    TEST_ASSERT_EQUAL_INT(OBD2_OK, s);
    /* Kratky timeout zrychli testy timeoutu — mock cas se posune jen o 200 ms
     * pri prazdne RX fronte, neni treba cekat defaultnich 2000 ms. */
    obd2_set_timeout(200);
}

/**
 * Uklid po kazdem testu — uvolni ISO-TP a obnovi cisty stav.
 * (Neni explicitni tearDown v unity_lite, takze to delame na zacatku
 * dalsiho setupu nebo rucne.)
 */
static void teardown_obd2(void)
{
    obd2_deinit();
}

/* ========================================================================= */
/*  Test 1: obd2_get_pid_raw -- unicast SF happy path                        */
/* ========================================================================= */

/*
 * Scenar: dotaz na PID $0C (otacky motoru) na fyzicke adrese 0x7E0.
 *   Request:  0x7E0 -> [02 01 0C CC CC CC CC CC]  (SF, len=2)
 *   Response: 0x7E8 -> [04 41 0C 1A F8 CC CC CC]  (SF, len=4, data: 0x1AF8 -> 1726 RPM)
 *
 * Overujeme, ze:
 *   - funkce vraci OBD2_OK
 *   - result->pid = 0x0C (echo PIDu)
 *   - result->data_len = 2 (dva datove bajty)
 *   - result->data[0] = 0x1A, result->data[1] = 0xF8
 *   - byl odeslan prave jeden ramec na adresu 0x7E0
 *   - PCI bajt SF ma spravnou delku (0x02 = SF|len=2)
 */
void test_obd2_get_pid_raw_rpm_happy_path(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x04, 0x41, 0x0C, 0x1A, 0xF8, 0xCC, 0xCC, 0xCC);

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x0C, &raw);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_HEX(0x0C, raw.pid);
    TEST_ASSERT_EQUAL_INT(2, raw.data_len);
    TEST_ASSERT_EQUAL_HEX(0x1A, raw.data[0]);
    TEST_ASSERT_EQUAL_HEX(0xF8, raw.data[1]);

    /* Odeslan presne 1 ramec (SF request) na fyzickou adresu */
    TEST_ASSERT_EQUAL_INT(1, mock_twai_get_tx_count());
    const twai_message_t *tx = mock_twai_get_tx_frame(0);
    TEST_ASSERT_EQUAL_HEX(0x7E0, tx->identifier);
    TEST_ASSERT_EQUAL_HEX(0x02, tx->data[0]);  /* SF|len=2 */
    TEST_ASSERT_EQUAL_HEX(0x01, tx->data[1]);  /* SID */
    TEST_ASSERT_EQUAL_HEX(0x0C, tx->data[2]);  /* PID */

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 2: obd2_get_pid_raw -- not-initialized guard                        */
/* ========================================================================= */

void test_obd2_get_pid_raw_not_initialized(void)
{
    mock_twai_reset();
    isotp_set_log_level(ISOTP_LOG_NONE);
    obd2_set_log_level(ISOTP_LOG_NONE);
    /* Nevolame obd2_init()! */
    /* Ale kvuli predchozim testum muze byt stav "initialized" -- zajisteme deinit. */
    obd2_deinit();

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x0C, &raw);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_NOT_INITIALIZED, st);
}

/* ========================================================================= */
/*  Test 3: obd2_get_pid_raw -- NULL result guard                            */
/* ========================================================================= */

void test_obd2_get_pid_raw_null_result(void)
{
    setup_obd2();

    obd2_status_t st = obd2_get_pid_raw(0x0C, NULL);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_INVALID_ARG, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 4: obd2_get_pid_raw -- PID echo mismatch                            */
/* ========================================================================= */

/*
 * ECU nam pro dotaz PID $0C (otacky) vrati odpoved s PID $0D (rychlost).
 * To je anomalie (napr. kolize na sbernici) a funkce ji musi detekovat
 * a vratit OBD2_ERR_RESPONSE_MALFORMED.
 */
void test_obd2_get_pid_raw_echo_mismatch(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x03, 0x41, 0x0D, 0x32, 0xCC, 0xCC, 0xCC, 0xCC);  /* PID $0D, ne $0C! */

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x0C, &raw);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_RESPONSE_MALFORMED, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 5: obd2_get_pid_raw -- negativni odpoved s NRC                      */
/* ========================================================================= */

/*
 * ECU odmitne pozadavek negativni odpovedi:
 *   [03 7F 01 12 CC CC CC CC]
 *   = SF|len=3, 7F (negative), 01 (Mode 01), 12 (subFunctionNotSupported)
 *
 * Overujeme:
 *   - funkce vraci OBD2_ERR_NEGATIVE_RESP
 *   - obd2_get_last_nrc() vraci spravny SID a NRC kod
 */
void test_obd2_get_pid_raw_negative_response_captures_nrc(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x03, 0x7F, 0x01, 0x12, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x5A, &raw);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_NEGATIVE_RESP, st);

    obd2_nrc_info_t nrc = obd2_get_last_nrc();
    TEST_ASSERT_EQUAL_HEX(0x01, nrc.request_sid);
    TEST_ASSERT_EQUAL_HEX(OBD2_NRC_SUB_FUNCTION_NOT_SUPPORTED, nrc.nrc);
    TEST_ASSERT_STRING_EQUAL("subFunctionNotSupported", obd2_nrc_str(nrc.nrc));

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 6: obd2_get_pid_raw -- timeout kdyz ECU neodpovi                    */
/* ========================================================================= */

void test_obd2_get_pid_raw_timeout(void)
{
    setup_obd2();
    /* RX fronta zustava prazdna — mock twai_receive posune simulovany cas
     * a vrati ESP_ERR_TIMEOUT. */

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x0C, &raw);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_TIMEOUT, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 7: obd2_get_pid -- kompletni dekodovany vysledek                    */
/* ========================================================================= */

/*
 * Mode 01 PID $0C, data 0x1AF8 -> (256*0x1A + 0xF8) * 0.25 = 6904/4 = 1726 RPM
 */
void test_obd2_get_pid_decoded_rpm(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x04, 0x41, 0x0C, 0x1A, 0xF8, 0xCC, 0xCC, 0xCC);

    obd2_pid_decoded_t dec;
    obd2_status_t st = obd2_get_pid(0x0C, &dec);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_HEX(0x0C, dec.pid);
    TEST_ASSERT_EQUAL_FLOAT(1726.0f, dec.value, 0.5f);
    TEST_ASSERT_STRING_EQUAL("Engine RPM", dec.name);
    TEST_ASSERT_STRING_EQUAL("rpm", dec.unit);

    teardown_obd2();
}

/*
 * Dekodovana teplota chladici kapaliny (PID $05), A=0x5A (90) -> 90-40 = 50 stupnu.
 */
void test_obd2_get_pid_decoded_coolant_temp(void)
{
    setup_obd2();

    /* Odpoved: [03 41 05 5A CC CC CC CC] = SF|len=3, 41 05 (echo), 5A (data) */
    mock_twai_inject_rx_frame(0x7E8,
        0x03, 0x41, 0x05, 0x5A, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_pid_decoded_t dec;
    obd2_status_t st = obd2_get_pid(0x05, &dec);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, dec.value, 0.01f);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 8: obd2_read_dtc -- zadny DTC ulozen (bezchybny stav)               */
/* ========================================================================= */

/*
 * Broadcast Mode 03. Odpoved ECU: [02 43 00 CC CC CC CC CC]
 *   SF|len=2, SID 43 (pozitiv odpoved na 03), pocet_DTC = 0
 *
 * Overujeme:
 *   - out_count = 0
 *   - funkce vraci OBD2_OK (nula DTC je legitimni dobry stav)
 *   - byl odeslan broadcast ramec na 0x7DF
 */
void test_obd2_read_dtc_no_dtcs_stored(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x02, 0x43, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_dtc_t dtcs[10];
    uint8_t count = 0xFF;
    obd2_status_t st = obd2_read_dtc(dtcs, 10, &count);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_INT(0, count);

    /* Prvni TX ramec musi byt broadcast */
    const twai_message_t *tx0 = mock_twai_get_tx_frame(0);
    TEST_ASSERT_NOT_NULL(tx0);
    TEST_ASSERT_EQUAL_HEX(0x7DF, tx0->identifier);
    TEST_ASSERT_EQUAL_HEX(0x01, tx0->data[0]);  /* SF|len=1 */
    TEST_ASSERT_EQUAL_HEX(0x03, tx0->data[1]);  /* SID Mode 03 */

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 9: obd2_read_dtc -- dva DTC v jednom ramci                          */
/* ========================================================================= */

/*
 * Broadcast Mode 03. Odpoved ma 2 DTC, vsechno se vejde do jednoho SF:
 *   [06 43 02 01 00 01 31 CC]
 *   = SF|len=6, SID 43, pocet=2, DTC1=[01 00]=P0100, DTC2=[01 31]=P0131
 *
 * P0100 = "Mass Air Flow Circuit Malfunction"
 * P0131 = "O2 Sensor Circuit Low Voltage (Bank 1, Sensor 1)"
 */
void test_obd2_read_dtc_two_dtcs_single_frame(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x06, 0x43, 0x02, 0x01, 0x00, 0x01, 0x31, 0xCC);

    obd2_dtc_t dtcs[10];
    uint8_t count = 0;
    obd2_status_t st = obd2_read_dtc(dtcs, 10, &count);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_STRING_EQUAL("P0100", dtcs[0].code);
    TEST_ASSERT_STRING_EQUAL("P0131", dtcs[1].code);
    TEST_ASSERT_EQUAL_HEX(0x01, dtcs[0].raw[0]);
    TEST_ASSERT_EQUAL_HEX(0x00, dtcs[0].raw[1]);
    TEST_ASSERT_EQUAL_HEX(0x01, dtcs[1].raw[0]);
    TEST_ASSERT_EQUAL_HEX(0x31, dtcs[1].raw[1]);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 10: obd2_read_dtc -- 5 DTC vyzaduje multi-frame                     */
/* ========================================================================= */

/*
 * 5 DTC = 10 datovych bajtu + 2 bajty hlavicky (SID, pocet) = 12 bajtu.
 * To se do SF nevejde (max 7 dat bajtu za PCI), takze ECU posle FF + 1 CF.
 *
 * Payload:
 *   [43 05 | 01 00 | 01 31 | 03 00 | 04 20 | C1 23]
 *   ^  ^    ^ P0100 ^ P0131 ^ P0300 ^ P0420 ^ U0123
 *   sid count
 *
 * FF (12 bajtu):  [10 0C 43 05 01 00 01 31]   (FF_DL=12, prvnich 6 bajtu payloadu)
 * CF (zbytek):    [21 03 00 04 20 C1 23 CC]   (SN=1, 6 bajtu + padding)
 *
 * ISO-TP vrstva mezi FF a CF automaticky posila FC (Flow Control, CTS).
 */
void test_obd2_read_dtc_five_dtcs_multiframe(void)
{
    setup_obd2();

    /* FF: FF_DL=12, prvnich 6 bajtu payloadu */
    mock_twai_inject_rx_frame(0x7E8,
        0x10, 0x0C, 0x43, 0x05, 0x01, 0x00, 0x01, 0x31);
    /* CF1: SN=1, dalsich 6 bajtu + 1 padding */
    mock_twai_inject_rx_frame(0x7E8,
        0x21, 0x03, 0x00, 0x04, 0x20, 0xC1, 0x23, 0xCC);

    obd2_dtc_t dtcs[10];
    uint8_t count = 0;
    obd2_status_t st = obd2_read_dtc(dtcs, 10, &count);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_INT(5, count);
    TEST_ASSERT_STRING_EQUAL("P0100", dtcs[0].code);
    TEST_ASSERT_STRING_EQUAL("P0131", dtcs[1].code);
    TEST_ASSERT_STRING_EQUAL("P0300", dtcs[2].code);
    TEST_ASSERT_STRING_EQUAL("P0420", dtcs[3].code);
    TEST_ASSERT_STRING_EQUAL("U0123", dtcs[4].code);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 11: obd2_read_dtc -- NULL ochrana                                   */
/* ========================================================================= */

void test_obd2_read_dtc_null_dtcs_pointer(void)
{
    setup_obd2();

    uint8_t count = 0;
    obd2_status_t st = obd2_read_dtc(NULL, 10, &count);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_INVALID_ARG, st);

    teardown_obd2();
}

void test_obd2_read_dtc_null_count_pointer(void)
{
    setup_obd2();

    obd2_dtc_t dtcs[10];
    obd2_status_t st = obd2_read_dtc(dtcs, 10, NULL);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_INVALID_ARG, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 12: obd2_clear_dtc -- uspesne smazani                               */
/* ========================================================================= */

/*
 * Mode 04 broadcast. Odpoved: [01 44 CC CC CC CC CC CC]
 *   = SF|len=1, SID 44 (pozitiv odpoved na 04)
 */
void test_obd2_clear_dtc_success(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x01, 0x44, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_status_t st = obd2_clear_dtc();
    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);

    /* Overime, ze jsme odeslali broadcast request Mode 04 */
    const twai_message_t *tx0 = mock_twai_get_tx_frame(0);
    TEST_ASSERT_NOT_NULL(tx0);
    TEST_ASSERT_EQUAL_HEX(0x7DF, tx0->identifier);
    TEST_ASSERT_EQUAL_HEX(0x01, tx0->data[0]);  /* SF|len=1 */
    TEST_ASSERT_EQUAL_HEX(0x04, tx0->data[1]);  /* SID Mode 04 */

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 13: obd2_clear_dtc -- odmitnuti ECU                                 */
/* ========================================================================= */

/*
 * Typicky scenar: uzivatel se pokusi smazat DTC pri bezicim motoru.
 * ECU odpovi NRC 0x22 conditionsNotCorrect:
 *   [03 7F 04 22 CC CC CC CC]
 */
void test_obd2_clear_dtc_rejected_conditions_not_correct(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x03, 0x7F, 0x04, 0x22, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_status_t st = obd2_clear_dtc();
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_NEGATIVE_RESP, st);

    obd2_nrc_info_t nrc = obd2_get_last_nrc();
    TEST_ASSERT_EQUAL_HEX(0x04, nrc.request_sid);
    TEST_ASSERT_EQUAL_HEX(OBD2_NRC_CONDITIONS_NOT_CORRECT, nrc.nrc);
    TEST_ASSERT_STRING_EQUAL("conditionsNotCorrect", obd2_nrc_str(nrc.nrc));

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 14: obd2_read_vin -- multi-frame (FF + 2xCF)                        */
/* ========================================================================= */

/*
 * VIN "WVWZZZ3CZWE123456" (17 ASCII znaku) pres Mode 09 InfoType 02.
 * Odpoved ma celkovou delku 20 bajtu:
 *   [49 02 01 'W' 'V' 'W' 'Z' 'Z' 'Z' '3' 'C' 'Z' 'W' 'E' '1' '2' '3' '4' '5' '6']
 *   SID=49, InfoType=02, NODI=01, pak 17 znaku VIN.
 *
 * Rozdeleni do ISO-TP ramcu (fyzicky transfer, ne broadcast -- Mode 09 vyzaduje unicast):
 *   FF:  [10 14 49 02 01 W V W]             (FF_DL=20, 6 bajtu payloadu)
 *   CF1: [21 Z Z Z 3 C Z W]                 (SN=1, 7 bajtu)
 *   CF2: [22 E 1 2 3 4 5 6]                 (SN=2, 7 bajtu)
 *
 * Mezi FF a CF nas kod odesle FC (Flow Control, CTS) na 0x7E0.
 */
void test_obd2_read_vin_multiframe_happy_path(void)
{
    setup_obd2();
    /* Delsi timeout -- ISO-TP multi-frame ma vlastni N_Cr timeout (typicky 150 ms),
     * a my potrebujeme, aby obd2 timeout byl vetsi nez suma dilcich timeoutu. */
    obd2_set_timeout(1000);

    /* FF: FF_DL = 20 (= 0x14), prvnich 6 bajtu payloadu */
    mock_twai_inject_rx_frame(0x7E8,
        0x10, 0x14, 0x49, 0x02, 0x01, 'W', 'V', 'W');
    /* CF1: SN=1, dalsich 7 bajtu */
    mock_twai_inject_rx_frame(0x7E8,
        0x21, 'Z', 'Z', 'Z', '3', 'C', 'Z', 'W');
    /* CF2: SN=2, poslednich 7 bajtu */
    mock_twai_inject_rx_frame(0x7E8,
        0x22, 'E', '1', '2', '3', '4', '5', '6');

    char vin[OBD2_VIN_LENGTH + 1];
    memset(vin, 0xAA, sizeof(vin));

    obd2_status_t st = obd2_read_vin(vin, sizeof(vin));
    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_STRING_EQUAL("WVWZZZ3CZWE123456", vin);

    /*
     * Overime, ze v TX zachytovaci je nekde FC ramec na 0x7E0.
     * Presna pozice zavisi na tom, zda se mezi pozadavkem a FC vlozily
     * i jine ramce, ale v nejjednodussim scenari:
     *   TX[0] = SF request (0x7E0)
     *   TX[1] = FC CTS   (0x7E0)
     */
    TEST_ASSERT_TRUE(mock_twai_get_tx_count() >= 2);
    const twai_message_t *fc = mock_twai_get_tx_frame(1);
    TEST_ASSERT_EQUAL_HEX(0x7E0, fc->identifier);
    TEST_ASSERT_EQUAL_HEX(0x30, fc->data[0]);  /* FC|CTS */

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 15: obd2_read_vin -- maly buffer odmitnut                           */
/* ========================================================================= */

void test_obd2_read_vin_buffer_too_small(void)
{
    setup_obd2();

    char vin[10];  /* Min. je OBD2_VIN_LENGTH+1 = 18 */
    obd2_status_t st = obd2_read_vin(vin, sizeof(vin));
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_INVALID_ARG, st);

    /* Nesmi nic odeslat — pozadavek se ani nezacne */
    TEST_ASSERT_EQUAL_INT(0, mock_twai_get_tx_count());

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 16: obd2_read_vin -- NULL buffer                                    */
/* ========================================================================= */

void test_obd2_read_vin_null_buffer(void)
{
    setup_obd2();

    obd2_status_t st = obd2_read_vin(NULL, 32);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_INVALID_ARG, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 17: obd2_read_ecu_name -- SF odpoved                                */
/* ========================================================================= */

/*
 * Mode 09 InfoType 0A, ECU vrati kratky nazev "ECM\0" (nekdy padding mezerami).
 * Pro jednoduchost zkousime 4-znakovy nazev, aby se vesel do SF.
 *
 * Odpoved: [07 49 0A 01 'E' 'C' 'M' 0x00]
 *   SF|len=7, SID 49, InfoType 0A, NODI=1, data "ECM\0"
 */
void test_obd2_read_ecu_name_short_sf(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x07, 0x49, 0x0A, 0x01, 'E', 'C', 'M', 0x00);

    char name[OBD2_ECU_NAME_MAX_LENGTH + 1];
    memset(name, 0xAA, sizeof(name));

    obd2_status_t st = obd2_read_ecu_name(name, sizeof(name));
    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    /* Nazev muze obsahovat null uvnitr (padding), ale zacina "ECM" */
    TEST_ASSERT_EQUAL_INT('E', name[0]);
    TEST_ASSERT_EQUAL_INT('C', name[1]);
    TEST_ASSERT_EQUAL_INT('M', name[2]);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 18: opakovana inicializace je idempotentni                          */
/* ========================================================================= */

/*
 * Cyklus init -> deinit -> init musi vrstvu vratit do cisteho stavu
 * (last_nrc je nulove). Pozn.: druhe volani obd2_init() na uz inicializovane
 * vrstve je v novem stacku zamerne no-op, takze pro reset stavu je nutne
 * mezi tim explicitne zavolat obd2_deinit().
 */
void test_obd2_init_is_idempotent(void)
{
    mock_twai_reset();
    isotp_set_log_level(ISOTP_LOG_NONE);
    obd2_set_log_level(ISOTP_LOG_NONE);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, obd2_init(500000, 5, 4));

    /* Vyrobime NRC stav. */
    obd2_set_timeout(100);
    mock_twai_inject_rx_frame(0x7E8,
        0x03, 0x7F, 0x01, 0x12, 0xCC, 0xCC, 0xCC, 0xCC);
    obd2_pid_raw_t raw;
    (void)obd2_get_pid_raw(0x0C, &raw);

    obd2_nrc_info_t n1 = obd2_get_last_nrc();
    TEST_ASSERT_EQUAL_HEX(0x12, n1.nrc);

    /* Plny cyklus deinit -> init musi resetovat NRC. */
    obd2_deinit();
    mock_twai_reset();
    TEST_ASSERT_EQUAL_INT(OBD2_OK, obd2_init(500000, 5, 4));

    obd2_nrc_info_t n2 = obd2_get_last_nrc();
    TEST_ASSERT_EQUAL_HEX(0x00, n2.nrc);
    TEST_ASSERT_EQUAL_HEX(0x00, n2.request_sid);

    obd2_deinit();
}

/* ========================================================================= */
/*  Test 19: obd2_read_dtc bez predchozi inicializace                        */
/* ========================================================================= */

void test_obd2_read_dtc_not_initialized(void)
{
    mock_twai_reset();
    isotp_set_log_level(ISOTP_LOG_NONE);
    obd2_set_log_level(ISOTP_LOG_NONE);
    obd2_deinit();

    obd2_dtc_t dtcs[10];
    uint8_t count = 0;
    obd2_status_t st = obd2_read_dtc(dtcs, 10, &count);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_NOT_INITIALIZED, st);
}

/* ========================================================================= */
/*  Test 20: obd2_clear_dtc bez predchozi inicializace                       */
/* ========================================================================= */

void test_obd2_clear_dtc_not_initialized(void)
{
    mock_twai_reset();
    isotp_set_log_level(ISOTP_LOG_NONE);
    obd2_set_log_level(ISOTP_LOG_NONE);
    obd2_deinit();

    obd2_status_t st = obd2_clear_dtc();
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_NOT_INITIALIZED, st);
}

/* ========================================================================= */
/*  Test 21: VIN malformed - prilis kratka odpoved                           */
/* ========================================================================= */

/*
 * EOBD vozidla ne vzdy podporuji VIN. Nektera ECU vrati zkracenou odpoved
 * (napr. jen 4 znaky misto 17). Implementace musi takovou odpoved odmitnout
 * s OBD2_ERR_RESPONSE_MALFORMED, ne ji tise prijmout (jinak by vystup
 * obsahoval nahodne bajty).
 *
 * SF len=7 je maximalni platny SF (1 B PCI + 7 B dat). Payload obsahuje
 *   [49 02 01 'W' 'V' 'W' '?']
 *  = SID/InfoType/NODI + jen 4 znaky VIN. Po stripnuti hlavicky zustanou
 *    4 bajty, coz je < OBD2_VIN_LENGTH (17) -> MALFORMED.
 */
void test_obd2_read_vin_short_response_is_malformed(void)
{
    setup_obd2();

    mock_twai_inject_rx_frame(0x7E8,
        0x07, 0x49, 0x02, 0x01, 'W', 'V', 'W', '?');

    char vin[OBD2_VIN_LENGTH + 1];
    obd2_status_t st = obd2_read_vin(vin, sizeof(vin));
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_RESPONSE_MALFORMED, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 22: DTC truncation - count vetsi nez max_count                       */
/* ========================================================================= */

/*
 * ECU posle 3 DTC, ale tester ma misto pro pouze 2. Implementace musi
 * vlozit prvni 2 do pole a vratit count=2 (NE zapsat za hranice pole!).
 *
 * Response: [08 43 03 01 00 01 31 03 00]   - SF|len=8, 3 DTCs
 *            P0100, P0131, P0300
 */
void test_obd2_read_dtc_truncates_to_max_count(void)
{
    setup_obd2();

    /* Pravdive: payload 8 bajtu, 3 DTC -> potrebuje multi-frame. */
    mock_twai_inject_rx_frame(0x7E8,
        0x10, 0x08, 0x43, 0x03, 0x01, 0x00, 0x01, 0x31);
    mock_twai_inject_rx_frame(0x7E8,
        0x21, 0x03, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC);

    /* Buffer pro pouze 2 DTC + sentinel pro overeni, ze se nezapisuje za hranice. */
    obd2_dtc_t dtcs[3];
    memset(&dtcs[2], 0xFE, sizeof(obd2_dtc_t));  /* sentinel ve treti pozici */

    uint8_t count = 0;
    obd2_status_t st = obd2_read_dtc(dtcs, 2, &count);

    TEST_ASSERT_EQUAL_INT(OBD2_OK, st);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_STRING_EQUAL("P0100", dtcs[0].code);
    TEST_ASSERT_STRING_EQUAL("P0131", dtcs[1].code);

    /* Sentinel ve dtcs[2] nesmi byt prepsany — ochrana pred overflow. */
    TEST_ASSERT_EQUAL_HEX(0xFE, dtcs[2].raw[0]);
    TEST_ASSERT_EQUAL_HEX(0xFE, dtcs[2].raw[1]);

    teardown_obd2();
}

/* ========================================================================= */
/*  Test 23: get_pid_raw - prilis kratka odpoved                              */
/* ========================================================================= */

/*
 * ECU posle odpoved [02 41 0C] (jen 3 bajty: SID+PID, zadna data). Implementace
 * musi takovou odpoved odmitnout s OBD2_ERR_RESPONSE_MALFORMED — nesmi zapsat
 * 0 bajtu do data[] a vracet OK, ani nic horsiho.
 */
void test_obd2_get_pid_raw_response_too_short(void)
{
    setup_obd2();

    /* Odpoved bez datovych bajtu */
    mock_twai_inject_rx_frame(0x7E8,
        0x02, 0x41, 0x0C, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC);

    obd2_pid_raw_t raw;
    obd2_status_t st = obd2_get_pid_raw(0x0C, &raw);
    TEST_ASSERT_EQUAL_INT(OBD2_ERR_RESPONSE_MALFORMED, st);

    teardown_obd2();
}

/* ========================================================================= */
/*  Registr testu pro test_main.c                                            */
/* ========================================================================= */

void run_obd2_tests(void)
{
    /* Cteni PID (unicast SF) */
    RUN_TEST(test_obd2_get_pid_raw_rpm_happy_path);
    RUN_TEST(test_obd2_get_pid_raw_not_initialized);
    RUN_TEST(test_obd2_get_pid_raw_null_result);
    RUN_TEST(test_obd2_get_pid_raw_echo_mismatch);
    RUN_TEST(test_obd2_get_pid_raw_negative_response_captures_nrc);
    RUN_TEST(test_obd2_get_pid_raw_timeout);
    RUN_TEST(test_obd2_get_pid_decoded_rpm);
    RUN_TEST(test_obd2_get_pid_decoded_coolant_temp);

    /* Cteni DTC (broadcast) */
    RUN_TEST(test_obd2_read_dtc_no_dtcs_stored);
    RUN_TEST(test_obd2_read_dtc_two_dtcs_single_frame);
    RUN_TEST(test_obd2_read_dtc_five_dtcs_multiframe);
    RUN_TEST(test_obd2_read_dtc_null_dtcs_pointer);
    RUN_TEST(test_obd2_read_dtc_null_count_pointer);
    RUN_TEST(test_obd2_read_dtc_not_initialized);

    /* Mazani DTC (broadcast) */
    RUN_TEST(test_obd2_clear_dtc_success);
    RUN_TEST(test_obd2_clear_dtc_rejected_conditions_not_correct);
    RUN_TEST(test_obd2_clear_dtc_not_initialized);

    /* Cteni VIN (unicast multi-frame) */
    RUN_TEST(test_obd2_read_vin_multiframe_happy_path);
    RUN_TEST(test_obd2_read_vin_buffer_too_small);
    RUN_TEST(test_obd2_read_vin_null_buffer);

    /* Cteni nazvu ECU */
    RUN_TEST(test_obd2_read_ecu_name_short_sf);

    /* Zivotni cyklus */
    RUN_TEST(test_obd2_init_is_idempotent);

    /* Malformovane vstupy / hranicni pripady */
    RUN_TEST(test_obd2_read_vin_short_response_is_malformed);
    RUN_TEST(test_obd2_read_dtc_truncates_to_max_count);
    RUN_TEST(test_obd2_get_pid_raw_response_too_short);
}
