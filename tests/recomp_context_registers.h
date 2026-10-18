/* Register fixture for runtime and bridge context host tests. */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS uint32_t g_fs_base = XBOX_TIB_MAIN, g_seh_ebp;
RECOMP_TLS int g_df, g_fp_top, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word = 0x037f, g_fp_cc = 0x4000;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
ptrdiff_t g_xbox_mem_offset;
