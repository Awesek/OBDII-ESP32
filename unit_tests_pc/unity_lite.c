/**
 * @file unity_lite.c
 * @brief Definice globalnich pocitadel pro unity_lite.h.
 */
#include "unity_lite.h"

int _unity_tests_run             = 0;
int _unity_tests_failed          = 0;
int _unity_current_test_failed   = 0;
const char *_unity_current_test_name = NULL;
