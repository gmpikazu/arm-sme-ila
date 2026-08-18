#include "arm.h"

// main.c includes this header, while files in test/ define the function
extern void test_quick(arm::ArmSme& sme);
extern void test_cstr_helper(arm::ArmSme& sme);
extern void test_slice_helper(arm::ArmSme& sme);
extern void test_uf_dram(arm::ArmSme& sme_DramLE, arm::ArmSme& sme_DramBE);

// SME instructions
extern void test_pstate(arm::ArmSme& sme);
extern void test_zero(arm::ArmSme& sme);
extern void test_mova(arm::ArmSme& sme);
extern void test_addha_addva(arm::ArmSme& sme);
extern void test_integer_outer_prod(arm::ArmSme& sme);
extern void test_spl_svl(arm::ArmSme& sme);
extern void test_float_outer_prod(arm::ArmSme& sme);
// needs both LE and BE DRAM implementations for testing
extern void test_load(arm::ArmSme& sme_DramLE, arm::ArmSme& sme_DramBE);
extern void test_store(arm::ArmSme& sme_DramLE, arm::ArmSme& sme_DramBE);

// SVE2 instructions
extern void test_psel(arm::ArmSme& sme);
