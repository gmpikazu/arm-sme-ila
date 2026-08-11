#include "arm.h"

// main.c includes this header, while files in test/ define the function
void test_quick(arm::ArmSme& sme);
void test_cstr_helper(arm::ArmSme& sme);
void test_slice_helper(arm::ArmSme& sme);
void test_pstate(arm::ArmSme& sme);
void test_zero(arm::ArmSme& sme);
void test_mova(arm::ArmSme& sme);
void test_addha_addva(arm::ArmSme& sme);
void test_integer_outer_prod(arm::ArmSme& sme);
void test_spl_svl(arm::ArmSme& sme);
void test_float_outer_prod(arm::ArmSme& sme);
// needs both LE and BE DRAM implementations for testing
void test_load(arm::ArmSme& sme_DramLE, arm::ArmSme& sme_DramBE);
void test_store(arm::ArmSme& sme_DramLE, arm::ArmSme& sme_DramBE);
