/* ONN 4K Pro v2 (S7D, kernel 5.15.196-android14-11-g3d26c188ef7e-ab14657224)
 * Code guards computed from aml_media.ko in vendor_boot ramdisk.
 *
 * NOTE: These hashes are from the UNRELOCATED .ko file. At runtime, the kernel
 * applies relocations (adrp/add pairs, bl targets, etc.) which change some
 * instruction bytes. The hashes MUST be recomputed from the actual loaded
 * module memory once a memory dump method is available. Until then, the
 * code guard check in fixes_init() should be bypassed for initial testing.
 *
 * To recompute from runtime memory, the module must read each function's bytes
 * from the loaded address and hash them.
 */
static const struct code_guard guards_ca[] = {
{"dvel_receiver_event_fun",0xffffffc001ace750UL,420,0x16869d333c999772ULL},
{"dv_toggle_frame",0xffffffc001931b44UL,232,0xdb7f19429b287d71ULL},
{"set_layer_display_canvas",0xffffffc0019776a0UL,3648,0xa1e3ddaf0cae925fULL},
{"dv_core1a_set",0xffffffc001ada228UL,3700,0x2dd15a809ff7e799ULL},
{"dv_core1b_set",0xffffffc001adb09cUL,1804,0x8efc365e14ad2737ULL},
{"VSYNC_WR_DV_REG",0xffffffc001aade38UL,144,0xe184fb7a31efeae9ULL},
{"VSYNC_WR_DV_REG_BITS",0xffffffc001aadec8UL,192,0x374c15cea6257e7eULL},
{"tv_amdv_el_info",0xffffffc001acbc5cUL,104,0x59d22cbe5d70598dULL},
{"amdolby_vision_debug_store",0xffffffc001acf054UL,3360,0x2191816c36d5f225ULL},
{"amdv_parse_metadata",0xffffffc001ac2310UL,472,0x8783423293654287ULL},
{"is_dovi_dual_layer_frame",0xffffffc001ab5e44UL,292,0xe8e54ec4ea70d44aULL},
{"uvm_get_hook_mod",0xffffffc001892c34UL,544,0x4f7780b5b27fa47aULL},
{"uvm_put_hook_mod",0xffffffc0018929b8UL,636,0x6b45317ff8534cc6ULL},
{"v4lvideo_get_vf",0xffffffc001a8c514UL,212,0xe96113ef4674b705ULL},
{"init_file_private_data",0xffffffc001a8b04cUL,124,0x6d5ab7dc055db3dbULL},
{"amdv_parse_metadata_v2_stb",0xffffffc001abea34UL,10320,0xa8c5bdf015fae5f2ULL},
{"READ_VPP_DV_REG",0xffffffc001aadf88UL,52,0x63bf9876866da9deULL},
{"WRITE_VPP_DV_REG_BITS",0xffffffc001aae004UL,84,0xe5430fa1a77d7c5eULL},
{"common_receiver_event_fun",0xffffffc0019856f8UL,2068,0xda7980d5202bf9e9ULL},
{"amdv_vf_put",0xffffffc001ab6740UL,704,0xe3fd77c6ca229edcULL},
{"dv_vf_light_unreg_provider",0xffffffc001ab0118UL,272,0x80d7d2186488aa8aULL},
{"amdv_wait_metadata_hw5",0xffffffc001aedaa0UL,1272,0x4ad20f78ca25be06ULL},
{"amdv_wait_metadata_v2",0xffffffc001ac2dacUL,1540,0x76992c1748dd9ec7ULL},
{"amdv_wait_metadata_v1",0xffffffc001ac2560UL,1744,0x03ae030ed11d1cd6ULL},
{"recv_common_dequeue_frame",0xffffffc001984878UL,2180,0xf7b29bd76d8a002bULL},
{"get_output_mute",0xffffffc0019701d0UL,12,0xd2a802c8e0b2613dULL},
{"apply_stb_core_settings",0xffffffc001ad72d8UL,12112,0xe335cf54edf3eabfULL},
{"amdolby_vision_process",0xffffffc001ac7b68UL,9736,0x0bec87428c2f5f47ULL},
};

/* US aml_media g570709ea67dc. Addresses use the same link base as guards_ca;
 * only their position relative to the text base is checked, so no hashes. */
static const struct code_guard guards_us[] = {
{"dvel_receiver_event_fun",0xffffffc001acee80UL,420,0},
{"dv_toggle_frame",0xffffffc001931cd8UL,232,0},
{"set_layer_display_canvas",0xffffffc001977834UL,3648,0},
{"dv_core1a_set",0xffffffc001ada958UL,3700,0},
{"dv_core1b_set",0xffffffc001adb7ccUL,1804,0},
{"VSYNC_WR_DV_REG",0xffffffc001aadfccUL,144,0},
{"VSYNC_WR_DV_REG_BITS",0xffffffc001aae05cUL,192,0},
{"tv_amdv_el_info",0xffffffc001acc38cUL,104,0},
{"amdolby_vision_debug_store",0xffffffc001acf784UL,3360,0},
{"amdv_parse_metadata",0xffffffc001ac2720UL,472,0},
{"is_dovi_dual_layer_frame",0xffffffc001ab6004UL,292,0},
{"uvm_get_hook_mod",0xffffffc001892c34UL,536,0},
{"uvm_put_hook_mod",0xffffffc0018929b8UL,628,0},
{"v4lvideo_get_vf",0xffffffc001a8c6a8UL,212,0},
{"init_file_private_data",0xffffffc001a8b1e0UL,124,0},
{"amdv_parse_metadata_v2_stb",0xffffffc001abeda0UL,10484,0},
{"READ_VPP_DV_REG",0xffffffc001aae11cUL,52,0},
{"WRITE_VPP_DV_REG_BITS",0xffffffc001aae198UL,84,0},
{"common_receiver_event_fun",0xffffffc00198588cUL,2036,0},
{"amdv_vf_put",0xffffffc001ab6958UL,704,0},
{"dv_vf_light_unreg_provider",0xffffffc001ab02acUL,272,0},
{"amdv_wait_metadata_hw5",0xffffffc001aee1d0UL,1272,0},
{"amdv_wait_metadata_v2",0xffffffc001ac326cUL,1540,0},
{"amdv_wait_metadata_v1",0xffffffc001ac2970UL,1920,0},
{"recv_common_dequeue_frame",0xffffffc001984a0cUL,2180,0},
{"get_output_mute",0xffffffc001970364UL,12,0},
{"apply_stb_core_settings",0xffffffc001ad7a08UL,12112,0},
{"amdolby_vision_process",0xffffffc001ac8190UL,10000,0},
};
