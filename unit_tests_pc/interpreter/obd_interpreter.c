/**
 * @file obd_interpreter.c
 * @brief Interaktivni interpret pro testovani OBD-II stacku na PC.
 *
 * Umoznuje vstrikovat CAN ramce (odpovedi ECU) a volat funkce tveho stacku.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#include "obd2.h"
#include "obd2_internal.h"
#include "isotp.h"
#include "mock_twai.h"

/* Pristup k internim vecem pro ucely testovani */

void print_supported_pids() {
    printf("--- CONTEXT: SUPPORTED PIDS ---\n");
    for (int i = 0; i < 8; i++) {
        if (_ctx.supported_pids[i] != 0) {
            printf("  Range 0x%02X: 0x%08X\n", i * 0x20 + 1, _ctx.supported_pids[i]);
        }
    }
}

void print_monitor_status(const obd2_monitor_status_t *s) {
    printf("--- FULL-STACK RESULT: MONITOR STATUS (0x01) ---\n");
    printf("Engine Type: %s | MIL: %s | DTCs: %d\n", 
           s->is_compression ? "DIESEL" : "PETROL",
           s->mil_on ? "ON" : "OFF",
           s->dtc_count);
    printf("Misfire:  %s | Fuel: %s | CCM: %s\n",
           s->misfire_sup ? (s->misfire_rdy ? "READY" : "NOT RDY") : "---",
           s->fuel_sys_sup ? (s->fuel_sys_rdy ? "READY" : "NOT RDY") : "---",
           s->ccm_sup ? (s->ccm_rdy ? "READY" : "NOT RDY") : "---");
}

int main() {
    char input[256];
    
    /* 1. Inicializace stacku (mock hardware) */
    obd2_init(500000, 4, 5); 
    obd2_set_log_level(ISOTP_LOG_DEBUG);

    _ctx.pids_queried = false;
    memset(_ctx.supported_pids, 0, sizeof(_ctx.supported_pids));

    printf("OBD-II Full-Stack Interpreter (Using your real C files)\n");
    printf("Commands:\n");
    printf("  respid ID B0 B1... -> Inject frame with specific ID\n");
    printf("  resp B0 B1...      -> Inject frame with ID 0x7E8\n");
    printf("  get PID            -> Call obd2_get_pid(PID)\n");
    printf("  get 01 01 all      -> Call multi-ECU monitor status\n");
    printf("  raw MODE PID       -> Call obd2_query_raw\n");
    printf("  setmask HEX8       -> Manually set supported PIDs mask\n");
    printf("  reset              -> Clear mock CAN queues\n");
    printf("  q                  -> Quit\n");

    while (1) {
        printf("\n> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        if (input[0] == 'q') break;

        char *ptr = input;
        while (*ptr && isspace(*ptr)) ptr++;
        if (!*ptr) continue;

        /* PRIORITA: respid musi byt pred resp */
        if (strncmp(ptr, "respid", 6) == 0) {
            unsigned int id = 0;
            unsigned int b[8] = {0};
            int n = sscanf(ptr, "respid %x %x %x %x %x %x %x %x %x", 
                           &id, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
            if (n >= 2) {
                uint8_t data[8];
                for(int i=0; i<8; i++) data[i] = (uint8_t)b[i];
                mock_twai_inject_rx((uint32_t)id, data, 8);
                printf("Mock CAN: Injected frame [ID 0x%03X]: ", id);
                for(int i=0; i<8; i++) printf("%02X ", data[i]);
                printf("\n");
            } else {
                printf("Usage: respid ID B0 B1 ... B7\n");
            }

        } else if (strncmp(ptr, "resp", 4) == 0) {
            unsigned int b[8] = {0};
            int n = sscanf(ptr, "resp %x %x %x %x %x %x %x %x", 
                           &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
            if (n >= 1) {
                uint8_t data[8];
                for(int i=0; i<8; i++) data[i] = (uint8_t)b[i];
                mock_twai_inject_rx(0x7E8, data, 8);
                printf("Mock CAN: Injected frame [ID 0x7E8]: ");
                for(int i=0; i<8; i++) printf("%02X ", data[i]);
                printf("\n");
            } else {
                printf("Usage: resp B0 B1 ... B7\n");
            }

        } else if (strncmp(ptr, "setmask", 7) == 0) {
            ptr += 7;
            while (*ptr && isspace(*ptr)) ptr++;
            uint32_t mask;
            if (sscanf(ptr, "%8x", &mask) == 1) {
                _ctx.supported_pids[0] = mask;
                _ctx.pids_queried = true;
                printf("Context: Supported PIDs set to 0x%08X\n", mask);
            }

        } else if (strncmp(ptr, "raw", 3) == 0) {
            unsigned int s, p;
            if (sscanf(ptr, "raw %x %x", &s, &p) == 2) {
                obd2_raw_response_t raw;
                printf("Stack: Manual query Mode 0x%02X PID 0x%02X...\n", s, p);
                obd2_status_t res = obd2_query_raw((uint8_t)s, (uint8_t)p, &raw);
                if (res == OBD2_OK) {
                    printf("RESULT: ECU 0x%03X responded with %u bytes: ", raw.rx_id, raw.data_len);
                    for(int i=0; i<raw.data_len; i++) printf("%02X ", raw.data[i]);
                    printf("\n");
                } else {
                    printf("RESULT: Error %s\n", obd2_status_str(res));
                }
            }

        } else if (strncmp(ptr, "get", 3) == 0) {
            char *cmd_ptr = ptr + 3;
            unsigned int m_val = 0x01, p_val = 0x00;
            int matches = sscanf(cmd_ptr, "%x %x", &m_val, &p_val);
            if (matches == 1) { p_val = m_val; m_val = 0x01; }
            
            uint8_t mode = (uint8_t)m_val;
            uint8_t pid = (uint8_t)p_val;

            if (mode == 0x01 && pid == 0x01) {
                if (strstr(cmd_ptr, "all")) {
                    printf("Stack: Calling monitor status ALL (broadcast)...\n");
                    obd2_monitor_status_list_t mon_list;
                    obd2_status_t res = obd2_get_monitor_status_all(&mon_list);
                    if (res == OBD2_OK && mon_list.count > 0) {
                        for(int i=0; i<mon_list.count; i++) {
                            printf("RESULT: ECU 0x%03X: MIL=%s, DTCs=%d, Type=%s\n", 
                                   mon_list.items[i].rx_id,
                                   mon_list.items[i].status.mil_on ? "ON" : "OFF",
                                   mon_list.items[i].status.dtc_count,
                                   mon_list.items[i].status.is_compression ? "Diesel" : "Gasoline");
                        }
                    } else {
                        printf("RESULT: Error %s (found %d)\n", obd2_status_str(res), mon_list.count);
                    }
                } else {
                    printf("Stack: Calling monitor status (physical)...\n");
                    obd2_monitor_status_t st;
                    obd2_status_t res = obd2_get_monitor_status(NULL, &st);
                    if (res == OBD2_OK) print_monitor_status(&st);
                    else printf("RESULT: Error %s\n", obd2_status_str(res));
                }
            } else if (mode == 0x09 && pid == 0x0A) {
                /* Speciální případ pro Scan Network (jména ECU) */
                printf("Stack: Scanning network (ECU names broadcast)...\n");
                obd2_ecu_name_list_t list;
                obd2_status_t res = obd2_read_ecu_names_all(&list);
                if (res == OBD2_OK && list.count > 0) {
                    for(int i=0; i<list.count; i++) {
                        printf("RESULT: ECU 0x%03X: Name=\"%s\"\n", list.items[i].rx_id, list.items[i].name);
                    }
                } else {
                    printf("RESULT: Error %s (found %d)\n", obd2_status_str(res), list.count);
                }
            } else if (mode == 0x03) {
                obd2_dtc_t dtcs[32]; uint8_t count = 0;
                obd2_status_t res = obd2_read_dtc(dtcs, 32, &count);
                if (res == OBD2_OK) {
                    printf("RESULT: Found %d DTCs: ", count);
                    for(int i=0; i<count; i++) printf("%s ", dtcs[i].code);
                    printf("\n");
                } else printf("RESULT: Error %s\n", obd2_status_str(res));
            } else if (mode == 0x09 && pid == 0x02) {
                char vin[18];
                if (obd2_read_vin(vin, 18) == OBD2_OK) printf("RESULT: VIN = %s\n", vin);
                else printf("RESULT: VIN Error\n");
            } else {
                obd2_pid_decoded_t dec;
                obd2_status_t res = obd2_get_pid(pid, &dec);
                if (res == OBD2_OK) printf("RESULT: PID 0x%02X [%s] = %.2f %s\n", pid, dec.name, dec.value, dec.unit);
                else printf("RESULT: Error %s\n", obd2_status_str(res));
            }

        } else if (strncmp(ptr, "reset", 5) == 0) {
            mock_twai_reset();
            printf("Mock CAN: Queues cleared.\n");
        }
    }
    return 0;
}
