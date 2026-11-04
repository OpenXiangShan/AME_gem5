CASE_BINARY := test_gemm_mixed_types
CASE_DESCRIPTION := Mixed input GEMM types, packing, rounding and exception checks
REQUIRED_MNEMONICS := msettyp asettyp mls.rm mss.rm mmulacc.2d mmulaccneg.2d mmulatacc.2d mmulataccneg.2d mmulbtacc.2d mmulbtaccneg.2d
include ../common/case_rules.mk

$(HOST_BINARY) $(TARGET_BINARY): $(wildcard $(CASE_DIR)/*.h)
