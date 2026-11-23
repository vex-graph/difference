#ifndef DIFFERENCE_TEST_H
#define DIFFERENCE_TEST_H

#include "vex.h"

/**
 * Named automated test suite entry for difference_test dispatch.
 *
 * string name - CLI selector name for the suite
 * fn (*run)()      - zero-argument suite entry point
 */
typedef struct SuiteEntry {
    string name;
    fn (*run)();
} SuiteEntry;

#endif
