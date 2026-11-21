/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ONN 4K Pro v2 (S7D, kernel 5.15.196) reversible equivalents of the
 * reviewed open-driver changes. Never replace the proprietary Dolby module.
 * Only alter calls in our session.
 *
 * Ported from signde's Fire TV Cube 3 (SC2/PS7702, kernel 5.4.125) port of djnice's dvfel.
 * Firmware-specific offsets live in fw_profiles[] (one per supported build);
 * an unknown build refuses to load.
 */
#include <linux/utsname.h>
#include <linux/kallsyms.h>
struct code_guard { const char *name; unsigned long addr; size_t size; u64 hash; };
#include <linux/amlogic/media/vout/vinfo.h>
#include "live_guards.h"

/*
 * Data addresses (vd_layer, amdv_mask, dolby_vision_flags, enable_fel, ...) are
 * decoded at load from live adrp/add pairs. VD1 and VD2 are vd_layer[0] and
 * vd_layer[1] (stride 0x1040 on both supported builds).
 */
/*
 * === Per-firmware profiles ===
 *
 * Every firmware-specific value lives in a profile, as an offset from the
 * aml_media text base. The base is derived at load from READ_VPP_DV_REG, so
 * nothing depends on where the module landed. A profile is selected only if
 * the kernel release AND every relocation-free instruction in .sites match;
 * an unknown firmware refuses to load.
 *
 * To add a firmware: take aml_media.ko from that box, re-derive every value
 * below, and append a profile. Do not reuse offsets from another build.
 */
struct fw_site { const char *sym; unsigned int off; u32 insn; };
/* An adrp + add/ldr pair in live code. The kernel has already relocated it, so
 * decoding it gives the true data address whatever the module layout is. */
struct fw_ref { const char *sym; unsigned int adrp, lo; };
struct fw_profile {
 const char *name, *release;
 const struct fw_site *sites; size_t nsites;
 const struct code_guard *guards; size_t nguards;
 unsigned long link_base;        /* base the guards[] addresses were taken at */
 unsigned long read_reg, write_bits, write_bits_now;
 struct fw_ref vd_layer, enable_fel, el_flags, new_el_flags, amdv_mask, dv_flags;
 unsigned long vd_stride;
 unsigned long canvas_lr, apply_core, core1a, core1b, core1b_end;
 unsigned int el_enable_off, meta_off, recv_off[2], peek_off;
 unsigned int vf_dual_off, src_fmt_off, v4l_flag_off, vf_pts_off, el_reg;
};

static const struct fw_site ca_15016191_sites[]={
 {"amdv_parse_metadata_v2_stb",0x1420,0x7100011f}, /* cmp w8,#0 */
 {"amdv_parse_metadata_v2_stb",0x1ac,0x12001f48},  /* and w8,w26,#0xff */
 {"dvel_receiver_event_fun",0x44,0x35000048},      /* cbnz w8 */
 {"dvel_receiver_event_fun",0x6c,0x35000768},      /* cbnz w8 */
 {"dv_toggle_frame",0x50,0x350000a8},              /* cbnz w8 */
};

static const struct fw_site us_15640252_sites[]={
 {"amdv_parse_metadata_v2_stb",0x14b0,0x7100011f}, /* cmp w8,#0 */
 {"amdv_parse_metadata_v2_stb",0x1f0,0x12001f48},  /* and w8,w26,#0xff */
 {"dvel_receiver_event_fun",0x44,0x35000048},      /* cbnz w8 */
 {"dvel_receiver_event_fun",0x6c,0x35000768},      /* cbnz w8 */
 {"dv_toggle_frame",0x50,0x350000a8},              /* cbnz w8 */
};

static const struct fw_profile fw_profiles[]={
 {
  .name="CA URO3.260203.009.A2/15016191 (aml_media g6171dcee9587)",
  .release="5.15.196-android14-11-g3d26c188ef7e-ab14657224",
  .sites=ca_15016191_sites,.nsites=ARRAY_SIZE(ca_15016191_sites),
  .guards=guards_ca,.nguards=ARRAY_SIZE(guards_ca),
  .link_base=0xffffffc001836000UL,
  .read_reg=0x277f88,.write_bits=0x277ec8,.write_bits_now=0x278004,
  .vd_layer={"dvel_swap_frame",0x90,0x94},
  .enable_fel={"is_dovi_dual_layer_frame",0x38,0x3c},
  .el_flags={"amdv_parse_metadata_v1",0x15c0,0x15c4},
  .new_el_flags={"tv_amdv_el_info",0x30,0x34},.el_reg=25,
  .amdv_mask={"is_amdv_graphic_on",0x58,0x5c},
  .dv_flags={"amdv_parse_metadata_v1",0xaac,0xab0},
  .vd_stride=0x1040,
  .canvas_lr=0x1150cc,  /* dvel_swap_frame, after bl set_layer_display_canvas */
  .apply_core=0x2a12d8,  /* apply_stb_core_settings: dv_core1a_set is inlined here */
  .core1a=0x2a4228,.core1b=0x2a509c,.core1b_end=0x2a57a8,
  .el_enable_off=0x1420,.meta_off=0x1a0,.recv_off={0x44,0x6c},.peek_off=0x50,
  .vf_dual_off=0x6d4,.src_fmt_off=0x6c0,.v4l_flag_off=0x1158,.vf_pts_off=0x30,
 },
 {
  .name="US URO3.260203.035.A1/15640252 (aml_media g570709ea67dc)",
  .release="5.15.196-android14-11-g3d26c188ef7e-ab14657224",
  .sites=us_15640252_sites,.nsites=ARRAY_SIZE(us_15640252_sites),
  .guards=guards_us,.nguards=ARRAY_SIZE(guards_us),
  .link_base=0xffffffc001836000UL,
  .read_reg=0x27811c,.write_bits=0x27805c,.write_bits_now=0x278198,
  .vd_layer={"dvel_swap_frame",0x90,0x94},
  .enable_fel={"is_dovi_dual_layer_frame",0x38,0x3c},
  .el_flags={"amdv_parse_metadata_v1",0x1718,0x171c},
  .new_el_flags={"tv_amdv_el_info",0x30,0x34},.el_reg=24,
  .amdv_mask={"is_amdv_graphic_on",0x58,0x5c},
  .dv_flags={"amdv_parse_metadata_v1",0xb6c,0xb70},
  .vd_stride=0x1040,
  .canvas_lr=0x115260,  /* dvel_swap_frame, after bl set_layer_display_canvas */
  .apply_core=0x2a1a08,  /* apply_stb_core_settings: dv_core1a_set is inlined here */
  .core1a=0x2a4958,.core1b=0x2a57cc,.core1b_end=0x2a5ed8,
  .el_enable_off=0x14b0,.meta_off=0x1e4,.recv_off={0x44,0x6c},.peek_off=0x50,
  .vf_dual_off=0x6d4,.src_fmt_off=0x6c0,.v4l_flag_off=0x1158,.vf_pts_off=0x30,
 },
};

static const struct fw_profile *fw;
static unsigned long fw_text;
static unsigned long fw_vd_layer, fw_enable_fel, fw_el_flags, fw_new_el_flags, fw_amdv_mask, fw_dv_flags;
#define FW_ADDR(field) (fw_text+fw->field)

#define VD1_ADDR  fw_vd_layer
#define VD2_ADDR  (fw_vd_layer+fw->vd_stride)

#define FEL_ENABLE_ADDR  fw_enable_fel

/* EL_FLAGS_ADDR = &dovi_setting.el_flag (dovi_setting @ .bss+0x600b0, field +0x3414).
 * Derived from the "inst0: el_flag: %d" / "update el_flag %d, melFlag %d" printk in
 * amdv_parse_metadata_v1, which loads ldrb [dovi_setting+0x3414].
 * +1 (el_halfsize_flag) is ASSUMED adjacent as on SC2 -- verify in IDA.
 * Runtime: bss base 0xffffffc001db75d0 + 0x600b0 + 0x3414. */
/* new_m_dovi_setting.input[0].el_flag: what dv_core1a_set uses in multi_dv_mode.
 * el_halfsize_flag is the next byte in struct private_info_s. */
#define EL_FLAGS_ADDR    fw_new_el_flags

#define AMDV_MASK_ADDR   fw_amdv_mask  /* amdv_mask */
#define DV_FLAGS_ADDR    fw_dv_flags   /* dolby_vision_flags */

static bool fix_session, saved_enable_fel;
static u32 (*read_dv_reg_fn)(u32);
static int (*write_dv_bits_fn)(u32,u32,u32,u32);
static int (*write_dv_bits_now_fn)(u32,u32,u32,u32);
static int (*write_dv_reg_fn)(u32,u32);

/* These targets are raw addresses, not aml_media CFI jump table entries, so a
 * CFI-checked indirect call to them would panic this CONFIG_CFI_CLANG kernel. */
static u32 __nocfi read_dv_reg(u32 reg){return read_dv_reg_fn(reg);}
static int __nocfi write_dv_reg(u32 reg,u32 val){return write_dv_reg_fn(reg,val);}
static int __nocfi write_dv_bits(u32 reg,u32 val,u32 start,u32 len)
{return write_dv_bits_fn(reg,val,start,len);}
static int __nocfi write_dv_bits_now(u32 reg,u32 val,u32 start,u32 len)
{return write_dv_bits_now_fn(reg,val,start,len);}
static u32 saved_vd2_bypass;
static bool vd2_changed;
static unsigned long vd2_route_writes;
static u32 saved_el_mux;
static bool mux_allowed, mux_changed;
static bool route_el = true;
module_param(route_el,bool,0444);
MODULE_PARM_DESC(route_el,"Experimental S7D VD2 to core1a EL input routing");

static bool prepare_el_route(void)
{
 u32 mux;
 if(mux_allowed)return true;
 if(((read_dv_reg(0x1a70)>>4)&7)!=0)return false;
 mux=(read_dv_reg(0x1a71)>>4)&3;
 if(mux!=3)return false;
 saved_el_mux=mux;
 saved_vd2_bypass=(read_dv_reg(0x1a84)>>4)&1;
 mux_allowed=true;
 pr_info("dvfel: stock S7D route ready, saved EL mux=%u VD2 bypass=%u\n",
         saved_el_mux,saved_vd2_bypass);
 return true;
}

static unsigned long fixed_receiver, fixed_peek, fixed_canvas, fixed_register;

static bool residual_bypass = true;
module_param(residual_bypass,bool,0444);
MODULE_PARM_DESC(residual_bypass,"Let the stock Dolby library accept FEL residual data during a paired FEL session");
static struct module *stock_dovi_owner;
static struct kprobe residual_probe;
static bool residual_probe_active;
static unsigned long residual_checks_bypassed;
static bool residual_unpaired = true;
module_param(residual_unpaired,bool,0644);
MODULE_PARM_DESC(residual_unpaired,"Allow the same residual check bypass during BL fallback within the FEL test session");
static unsigned long residual_unpaired_hits;

static int residual_pre(struct kprobe *p, struct pt_regs *r)
{
 /* Emulate falling through this CBZ only. Do NOT rewrite RPU metadata,
  * coefficients, the file, or any other instruction/validation result. */
 if(READ_ONCE(fix_session) && READ_ONCE(g.active) &&
    (READ_ONCE(*(u8 *)EL_FLAGS_ADDR) || residual_unpaired) && !(u32)r->regs[8]){
  if (!READ_ONCE(*(u8 *)EL_FLAGS_ADDR)) residual_unpaired_hits++;
  r->regs[8]=1;
  residual_checks_bypassed++;
 }
 return 0;
}

/*
 * commit_reg in the stock Dolby library rejects FEL residual data with -2, which
 * leaves the DV cores unconfigured and the output flat green. The probe sits on
 * the cbz that takes that error path. It is checked instruction by instruction,
 * and an unknown library refuses to load rather than producing a broken picture.
 */
#define DOVI_COMMIT_REG "dovi_s7d_5_15_stb26:commit_reg"
#define DOVI_RESIDUAL_OFF 0xa6c
#define DOVI_ERROR_OFF 0x14f0
static unsigned long lookup_name(const char *name);
static int residual_init(void)
{
 static const u32 expected[]={0x9ba65308,0xf9402508,0x39527508,0x34005428,0x52800029};
 static const u32 expected_error[]={0x12800020,0x140003cf};
 u32 actual[ARRAY_SIZE(expected)],error[ARRAY_SIZE(expected_error)];
 unsigned long addr;int ret;
 if(!residual_bypass)return 0;
 addr=lookup_name(DOVI_COMMIT_REG);
 if(!addr){pr_err("dvfel: stock Dolby commit_reg not found\n");return -ENODEV;}
 if(copy_from_kernel_nofault(actual,(void *)(addr+DOVI_RESIDUAL_OFF-12),sizeof(actual)) ||
    copy_from_kernel_nofault(error,(void *)(addr+DOVI_ERROR_OFF),sizeof(error)) ||
    memcmp(actual,expected,sizeof(actual)) || memcmp(error,expected_error,sizeof(error))){
  pr_err("dvfel: stock Dolby residual check does not match this library\n");
  return -EINVAL;
 }
 residual_probe.addr=(kprobe_opcode_t *)(addr+DOVI_RESIDUAL_OFF);
 residual_probe.pre_handler=residual_pre;
 ret=register_kprobe(&residual_probe);
 if(ret){pr_err("dvfel: residual probe failed %d\n",ret);return ret;}
 residual_probe_active=true;
 pr_info("dvfel: verified stock Dolby residual check\n");
 return 0;
}

static bool fel_composer_mode;
module_param(fel_composer_mode,bool,0444);
MODULE_PARM_DESC(fel_composer_mode,"Session-only core1a composer mode matching working CE FEL reference");
static bool composer_mode_changed;
static unsigned long composer_mode_writes;
static bool composer = true;
module_param(composer, bool, 0444);
MODULE_PARM_DESC(composer,"Enable hardware composition during test session (default true)");

static int branch_pre(struct kprobe *p, struct pt_regs *r)
{
 if(READ_ONCE(fix_session) || READ_ONCE(g.closing)){ r->regs[8]=0; fixed_receiver++; }
 return 0;
}

static int peek_pre(struct kprobe *p, struct pt_regs *r)
{
 if(READ_ONCE(fix_session)){r->regs[8]=0;fixed_peek++;}
 return 0;
}

static int canvas_pre(struct kprobe *p, struct pt_regs *r)
{
 unsigned long lr=r->regs[30];
 /* Only dvel_swap_frame passes &vd_layer[0] + EL vf; the layer_swap_frame
  * sites carry the live BL layer and must not be redirected. */
 if(READ_ONCE(fix_session) && r->regs[0]==VD1_ADDR && lr==FW_ADDR(canvas_lr)){
  r->regs[0]=VD2_ADDR;fixed_canvas++;
 }
 return 0;
}

static bool coherent_bl_fallback;
module_param(coherent_bl_fallback,bool,0644);
MODULE_PARM_DESC(coherent_bl_fallback,"Synchronize composer and EL geometry for BL-only fallback; default off");
static bool fallback_precommit = true;
module_param(fallback_precommit,bool,0644);
MODULE_PARM_DESC(fallback_precommit,"Apply coherent fallback mode before core configuration latch");
static int tracked_composer_mode = -1;
static unsigned long fallback_mode_skips, fallback_geometry_clears;
static unsigned long fallback_native_mode_writes;
static unsigned long fallback_precommit_writes;
static unsigned long fallback_mode_writes;
static int last_route_enabled = -1;

/* Diagnostic: which DV registers the driver programs during a session. */
static bool trace_regs;
module_param(trace_regs,bool,0644);
MODULE_PARM_DESC(trace_regs,"Record DV register writes made during a session (debugfs regs)");
#define REG_TRACE_MAX 192
static struct reg_trace {
 u32 reg, first, last, changes, count;
 u8 start, len, bits, fn;
} reg_traces[REG_TRACE_MAX];
static int reg_trace_n;
static unsigned long reg_trace_lost;
static DEFINE_SPINLOCK(reg_trace_lock);
static unsigned long mt_total,mt_paired,mt_dual,mt_aux,mt_forced,mt_reqel;
static unsigned long ee_total,ee_paired,ee_el,ee_paired_el;
static unsigned long f3_total,f3_new_el,f3_el_bit;
static unsigned long path_el_fixed,path_el_forced;
static bool path_changed;

static bool fel_el_enabled(void)
{
 return READ_ONCE(*(u8 *)EL_FLAGS_ADDR) && (READ_ONCE(*(u32 *)AMDV_MASK_ADDR)&1) &&
        !(READ_ONCE(*(u32 *)DV_FLAGS_ADDR)&0x100);
}

static u8 reg_trace_fn(unsigned long lr)
{
 if(lr>FW_ADDR(apply_core) && lr<FW_ADDR(core1a))return 1;
 if(lr>FW_ADDR(core1a) && lr<FW_ADDR(core1b))return 2;
 if(lr>FW_ADDR(core1b) && lr<FW_ADDR(core1b_end))return 3;
 return 0;
}

static void reg_trace_add(u32 reg,u32 val,u8 start,u8 len,u8 bits,unsigned long lr)
{
 unsigned long flags;int i;u8 fn;
 if(!READ_ONCE(trace_regs) || !READ_ONCE(fix_session))return;
 /* skip the per-frame DM and LUT payload registers */
 if((reg&0xff000000) && (reg&0xffffff)>=6 && (reg&0xffffff)<=0xef)return;
 if((reg&0xffffff)==0xff)return;
 fn=reg_trace_fn(lr);
 spin_lock_irqsave(&reg_trace_lock,flags);
 for(i=0;i<reg_trace_n;i++){
  struct reg_trace *t=&reg_traces[i];
  if(t->reg==reg && t->start==start && t->len==len && t->bits==bits && t->fn==fn){
   if(t->last!=val)t->changes++;
   t->last=val;t->count++;
   goto out;
  }
 }
 if(reg_trace_n<REG_TRACE_MAX){
  struct reg_trace *t=&reg_traces[reg_trace_n++];
  t->reg=reg;t->first=t->last=val;t->count=1;t->changes=0;
  t->start=start;t->len=len;t->bits=bits;t->fn=fn;
 } else reg_trace_lost++;
out:
 spin_unlock_irqrestore(&reg_trace_lock,flags);
}

static bool lldv_hdr10;
module_param(lldv_hdr10,bool,0644);
MODULE_PARM_DESC(lldv_hdr10,"Signal Dolby Vision low-latency output to the display as HDR10");
static unsigned int lldv_peak_nits=499;
module_param(lldv_peak_nits,uint,0644);
MODULE_PARM_DESC(lldv_peak_nits,"Peak luminance in nits reported in the HDR10 metadata");
static bool lldv_force_422;
module_param(lldv_force_422,bool,0644);
MODULE_PARM_DESC(lldv_force_422,"Build every HDMI output format as YCbCr 4:2:2 12-bit");
static unsigned int lldv_meta;
module_param(lldv_meta,uint,0644);
MODULE_PARM_DESC(lldv_meta,"HDR10 metadata source: 0 = tone mapping target, 1 = the movie's own values");
static unsigned int lldv_src_max_lum;
module_param(lldv_src_max_lum,uint,0644);
MODULE_PARM_DESC(lldv_src_max_lum,"Movie mastering display peak in nits");
static unsigned int lldv_src_min_lum;
module_param(lldv_src_min_lum,uint,0644);
MODULE_PARM_DESC(lldv_src_min_lum,"Movie mastering display black in 0.0001 nits");
static unsigned int lldv_src_cll;
module_param(lldv_src_cll,uint,0644);
MODULE_PARM_DESC(lldv_src_cll,"Movie MaxCLL in nits");
static unsigned int lldv_src_fall;
module_param(lldv_src_fall,uint,0644);
MODULE_PARM_DESC(lldv_src_fall,"Movie MaxFALL in nits");
static unsigned int lldv_limit_nits;
module_param(lldv_limit_nits,uint,0644);
MODULE_PARM_DESC(lldv_limit_nits,"Upper limit for the reported peak, MaxCLL and MaxFALL in nits, 0 = off");
static unsigned long lldv_ll_seen,lldv_sent;

static unsigned long comp_w_in,comp_w_out,comp_w_out_lr;static u32 comp_w_out_val;
static bool comp_seen_el;
/* A player opens a FEL session only after its RPU parser saw a FEL stream, so composer
 * mode 1 written then is never wanted. Players such as VLC show the first base-layer
 * frames before their EL decoder has output, which is when the library writes it. */
static bool comp_assume_fel=true;
module_param(comp_assume_fel,bool,0644);
MODULE_PARM_DESC(comp_assume_fel,"Keep FEL composing from the first frame of a player FEL session, before the first EL arrives");
static unsigned long comp_assumed;
struct comp_ev{u32 frame;u8 kind,val_in,val_out,en;u32 lroff;};
static struct comp_ev comp_ev[24];static int comp_ev_n;static unsigned long comp_live1,comp_live0;
static void comp_log(u8 kind,u32 vi,u32 vo,bool en,unsigned long lr)
{
 int i=READ_ONCE(comp_ev_n);
 if(i>=ARRAY_SIZE(comp_ev))return;
 comp_ev[i].frame=f3_total;comp_ev[i].kind=kind;comp_ev[i].val_in=vi;comp_ev[i].val_out=vo;
 comp_ev[i].en=en;comp_ev[i].lroff=lr?(u32)(lr-FW_ADDR(apply_core)):0;WRITE_ONCE(comp_ev_n,i+1);
}
static int regs_show(struct seq_file *m,void *v)
{
 static const char *const fn[]={"other","apply","core1a","core1b"};
 static const u32 live[]={0x1a70,0x1a71,0x1a83,0x1a84,0x1a0c,0x01000001,0x01000003,0x01000032,0x010000f3};
 unsigned long flags;int i;
 seq_printf(m,"meta total=%lu paired=%lu dual=%lu aux=%lu reqel=%lu forced=%lu\n",mt_total,mt_paired,mt_dual,mt_aux,mt_reqel,mt_forced);
 seq_printf(m,"gate total=%lu paired=%lu el=%lu paired_el=%lu\n",ee_total,ee_paired,ee_el,ee_paired_el);
 seq_printf(m,"core1a_f3 total=%lu new_el=%lu el_bit=%lu\n",f3_total,f3_new_el,f3_el_bit);
 seq_printf(m,"path_ctrl el_fixed=%lu el_forced=%lu\n",path_el_fixed,path_el_forced);
 seq_printf(m,"lldv force422=%d hdr10=%d nits=%u meta=%u src=%u/%u/%u/%u limit=%u ll_seen=%lu sent=%lu\n",lldv_force_422,lldv_hdr10,lldv_peak_nits,lldv_meta,lldv_src_max_lum,lldv_src_min_lum,lldv_src_cll,lldv_src_fall,lldv_limit_nits,lldv_ll_seen,lldv_sent);
 seq_printf(m,"cur_el_flag=%u new_el_flag=%u el_half=%u enable_fel=%u amdv_mask=%x dv_flags=%x session=%d\n",
            READ_ONCE(*(u8 *)fw_el_flags),READ_ONCE(*(u8 *)EL_FLAGS_ADDR),READ_ONCE(*(u8 *)(EL_FLAGS_ADDR+1)),
            READ_ONCE(*(u8 *)FEL_ENABLE_ADDR),READ_ONCE(*(u32 *)AMDV_MASK_ADDR),
            READ_ONCE(*(u32 *)DV_FLAGS_ADDR),READ_ONCE(fix_session));
 for(i=0;i<ARRAY_SIZE(live);i++)seq_printf(m,"live %08x = %08x\n",live[i],read_dv_reg(live[i]));
 seq_printf(m,"composer writes in=%lu out=%lu out_lr=%pS out_val=%x\n",comp_w_in,comp_w_out,(void *)comp_w_out_lr,comp_w_out_val);
 seq_printf(m,"composer live1=%lu live0=%lu events=%d assumed=%lu\n",comp_live1,comp_live0,comp_ev_n,comp_assumed);
 for(i=0;i<comp_ev_n;i++)seq_printf(m," ev frame=%u %s in=%u out=%u en=%u lroff=%x\n",comp_ev[i].frame,
   comp_ev[i].kind==0?"lib":comp_ev[i].kind==1?"fix":"stop",comp_ev[i].val_in,comp_ev[i].val_out,comp_ev[i].en,comp_ev[i].lroff);
 spin_lock_irqsave(&reg_trace_lock,flags);
 seq_printf(m,"traced %d lost %lu\n",reg_trace_n,reg_trace_lost);
 for(i=0;i<reg_trace_n;i++){
  const struct reg_trace *t=&reg_traces[i];
  seq_printf(m,"%-6s %s %08x",fn[t->fn],t->bits?"bits":"full",t->reg);
  if(t->bits)seq_printf(m,"[%u:%u]",t->start,t->len);
  seq_printf(m," n=%u changes=%u first=%x last=%x\n",t->count,t->changes,t->first,t->last);
 }
 spin_unlock_irqrestore(&reg_trace_lock,flags);
 return 0;
}

static u64 dvreg_writes; /* every VSYNC_WR_DV_REG call; read by timing.h */
static bool restore_pending; /* RDMA copies of the stop-time restore still to be queued */
static unsigned long restore_vsync,restore_direct,restore_skipped,restore_libregs,lib_overflow;
/* Library values of the core registers reg_pre changed this session (vsync path only). */
#define LIB_REGS 16
static struct { u32 reg, lib; } lib_regs[LIB_REGS];
static int lib_nregs;
static void lib_note(u32 reg,u32 lib,bool changed)
{
 int i;
 for(i=0;i<lib_nregs;i++)
  if(lib_regs[i].reg==reg){lib_regs[i].lib=lib;return;}
 if(!changed)return;
 if(lib_nregs<LIB_REGS){lib_regs[lib_nregs].reg=reg;lib_regs[lib_nregs].lib=lib;lib_nregs++;
  pr_info("dvfel: lib reg %08x lib value %08x changed\n",reg,lib);}
 else lib_overflow++;
}
static void fixes_restore_regs(void);
static void restore_work_fn(struct work_struct *w);
static DECLARE_DELAYED_WORK(restore_work,restore_work_fn);
static int reg_pre(struct kprobe *p, struct pt_regs *r)
{
 unsigned long lr=r->regs[30];u32 reg=r->regs[0], val=r->regs[1], off=reg&0xffffff;
 bool half, enabled;
 dvreg_writes++;
 if(unlikely(READ_ONCE(restore_pending)) && !READ_ONCE(fix_session) &&
    lr>FW_ADDR(apply_core) && lr<FW_ADDR(core1b_end)){
  WRITE_ONCE(restore_pending,false);
  pr_info("dvfel: restore at vsync: %d lib regs, path=%d vd2=%d mux=%d comp=%d, lr=+%lx\n",
          lib_nregs,path_changed,vd2_changed,mux_changed,composer_mode_changed,lr-FW_ADDR(apply_core));
  fixes_restore_regs();restore_vsync++;
 }
 reg_trace_add(reg,val,0,0,0,lr);
 if(reg==0x01000032){
  if(lr>FW_ADDR(apply_core) && lr<FW_ADDR(core1b_end))comp_w_in++;
  else {comp_w_out++;WRITE_ONCE(comp_w_out_lr,lr);WRITE_ONCE(comp_w_out_val,val);}
 }
 if(!READ_ONCE(fix_session) || !composer)return 0;
 /* Only writes made from dv_core1a_set / dv_core1b_set (apply_core..core1b_end). */
 if(!(lr>FW_ADDR(apply_core) && lr<FW_ADDR(core1b_end)))return 0;
 if((reg&0xff000000)!=0x01000000 && (reg&0xff000000)!=0x02000000 &&
    (reg&0xff000000)!=0x20000000)return 0;
 half=READ_ONCE(*(u8 *)(EL_FLAGS_ADDR+1));
 enabled=READ_ONCE(*(u8 *)EL_FLAGS_ADDR) &&
         (READ_ONCE(*(u32 *)AMDV_MASK_ADDR)&1) &&
         !(READ_ONCE(*(u32 *)DV_FLAGS_ADDR)&0x100);
 if (coherent_bl_fallback && !enabled) {
  if (half && off==0xf3 && (reg&0xff000000)==0x01000000)
   fallback_geometry_clears++;
  half=false;
 }
 if(off==3 && val==1 && (reg&0xff000000)==0x01000000 &&
    coherent_bl_fallback && fallback_precommit) {
  if (READ_ONCE(tracked_composer_mode) != (enabled?0:1)) {
   write_dv_bits(0x01000032,enabled?0:1,0,1);
   WRITE_ONCE(tracked_composer_mode,enabled?0:1);
   composer_mode_changed=true;
   fallback_mode_writes++;fallback_precommit_writes++;
  } else fallback_mode_skips++;
 }
 else if(off==1){val=(val&~8U)|(half?8:0);}
 else if(off==0xf3){
  if((reg&0xff000000)==0x01000000){
   f3_total++;if(READ_ONCE(*(u8 *)EL_FLAGS_ADDR))f3_new_el++;if(val&2)f3_el_bit++;
   /* If the library left the composer mode at 1 (written while the EL looked off), set it
    * back with one full write, as the library writes it; bit writes do not stick here and
    * the register reads back unreliably, so track the last value written instead.
    * Unknown (-1) counts too: with LLDV on since boot the library wrote its 1 before the
    * session and never writes it again, so the first EL frame of a session writes 0 once. */
   if(fel_composer_mode && enabled && !coherent_bl_fallback &&
      READ_ONCE(tracked_composer_mode)!=0){
    write_dv_reg(0x01000032,0);
    WRITE_ONCE(tracked_composer_mode,0);
    composer_mode_changed=true;composer_mode_writes++;
    comp_log(1,1,0,enabled,0);
   }
   /* AMDV_PATH_CTRL bit 1: 0 feeds VD2 to the core1a EL input */
   if(enabled){write_dv_bits(0x1a0c,0,1,1);path_changed=true;path_el_forced++;}
  }
  val=(val&~0x36U)|(half?0x34:0)|((enabled&&(val&1))?2:0);
  if (coherent_bl_fallback && !fallback_precommit && (reg&0xff000000)==0x01000000) {
   write_dv_bits(0x01000032,enabled?0:1,0,1);
   WRITE_ONCE(tracked_composer_mode,enabled?0:1);
   composer_mode_changed=true;fallback_mode_writes++;
  }
  if((reg&0xff000000)==0x01000000 && route_el && (mux_allowed || (enabled && prepare_el_route()))){
   if (last_route_enabled != enabled) {
    pr_info("dvfel: route enabled=%d paired=%d queue=%d bl=%px\n",
      enabled,g.bl_found,g.nq,g.bl_vf);
    last_route_enabled=enabled;
   }
   write_dv_bits(0x1a71,enabled?1:saved_el_mux,4,2);
   mux_changed=true;
   write_dv_bits(0x1a84,enabled?0:saved_vd2_bypass,4,1);
   vd2_changed=true;vd2_route_writes++;
  }
 }
 else if(off==0x32 && (reg&0xff000000)==0x01000000){
  {u32 vi=val;
  /* The library writes the composer mode only on the first frames of a session and then
   * keeps its own copy. One of those frames can report the EL as off, and a 1 written then
   * disables FEL for the whole session, so once the EL has been on, keep FEL composing.
   * Display-led output reports the EL as off on all of those frames, so a paired
   * dual-layer metadata frame in this session (meta_observe) also counts. */
  if(enabled)WRITE_ONCE(comp_seen_el,true);
  if(fel_composer_mode && val==1 &&
     (READ_ONCE(comp_seen_el) || (READ_ONCE(comp_assume_fel) && READ_ONCE(g.active)))){
   if(!READ_ONCE(comp_seen_el))comp_assumed++;
   val=0;composer_mode_changed=true;composer_mode_writes++;
  }
  comp_log(0,vi,val,enabled,lr);}
  WRITE_ONCE(tracked_composer_mode,val&1);
  fallback_native_mode_writes++;
 }
 else return 0;
 /* Remember the library's own value of every core register changed here (and keep it
  * current while the library writes it), so fixes_stop can put it back. */
 if(val!=r->regs[1]){lib_note(reg,r->regs[1],true);r->regs[1]=val;fixed_register++;}
 else lib_note(reg,val,false);
 return 0;
}

static int vd2_bits_pre(struct kprobe *p,struct pt_regs *r)
{
 reg_trace_add(r->regs[0],r->regs[1],r->regs[2],r->regs[3],1,r->regs[30]);
 if(READ_ONCE(fix_session) && READ_ONCE(g.active) && composer && r->regs[0]==0x1a0c &&
    r->regs[2]==1 && r->regs[3]==1 && r->regs[1] && fel_el_enabled()){
  r->regs[1]=0;path_changed=true;path_el_fixed++;
 }
 if(READ_ONCE(fix_session) && READ_ONCE(g.active) && composer && route_el &&
    r->regs[0]==0x1a84 && r->regs[2]==4 && r->regs[3]==1 &&
    READ_ONCE(*(u8 *)EL_FLAGS_ADDR) &&
    (READ_ONCE(*(u32 *)AMDV_MASK_ADDR)&1) &&
    !(READ_ONCE(*(u32 *)DV_FLAGS_ADDR)&0x100) && prepare_el_route()){
  r->regs[1]=0;vd2_changed=true;vd2_route_writes++;
 }
 return 0;
}

static unsigned int meta_seen;
static unsigned int meta_unpaired;
static int meta_observe(struct kprobe *p, struct pt_regs *r)
{
 u32 el=0, size=0; u8 dual=0; void *aux=NULL; unsigned long flags; bool paired;
 if (!READ_ONCE(fix_session)) return 0;
 /* struct provider_aux_req_s req is at x29-0x40: aux_buf -0x30, aux_size -0x28,
  * dv_enhance_exist -0x24. The probe sits at the merge point after the aux data
  * is fetched and before the first read of dv_enhance_exist. x20 is the vframe;
  * the dual flag is src_fmt.dual_layer (src_fmt+20). */
 copy_from_kernel_nofault(&el,(void *)(r->regs[29]-0x24),4);
 copy_from_kernel_nofault(&size,(void *)(r->regs[29]-0x28),4);
 copy_from_kernel_nofault(&aux,(void *)(r->regs[29]-0x30),8);
 copy_from_kernel_nofault(&dual,(void *)(r->regs[20]+fw->vf_dual_off),1);
 spin_lock_irqsave(&g.lock,flags);
 paired=g.active && g.bl_found && g.bl_vf==(struct vframe_s *)r->regs[20];
 spin_unlock_irqrestore(&g.lock,flags);
 mt_total++;if(paired)mt_paired++;if(dual)mt_dual++;if(aux&&size)mt_aux++;if(el)mt_reqel++;
 if (paired && dual && aux && size){
  WRITE_ONCE(*(u32 *)(r->regs[29]-0x24),1);mt_forced++;
  WRITE_ONCE(comp_seen_el,true);
 }
 if (!paired && meta_unpaired++ < 40)
  pr_info("dvfel: unpaired metadata toggle=%lu reqel=%u dual=%u size=%u vf=%px cached=%px found=%d queue=%d\n",
   r->regs[26]&255,el,dual,size,(void *)r->regs[20],g.bl_vf,g.bl_found,g.nq);
 if(meta_seen++ < 8)
  pr_info("dvfel: metadata gate toggle=%lu reqel=%u dual=%u size=%u paired=%d\n",
   r->regs[26]&255,el,dual,size,paired);
 return 0;
}

static int el_enable_pre(struct kprobe *p, struct pt_regs *r)
{
 unsigned long flags; bool paired;
 if(!READ_ONCE(fix_session))return 0;
 spin_lock_irqsave(&g.lock,flags);
 paired=g.active && g.bl_found && g.bl_vf==(struct vframe_s *)r->regs[20];
 spin_unlock_irqrestore(&g.lock,flags);
 ee_total++;if(paired)ee_paired++;
 if((u32)r->regs[fw->el_reg]){ee_el++;if(paired)ee_paired_el++;}
 if(paired)r->regs[8]=1;
 return 0;
}

/*
 * === KPROBE TABLE ===
 * Offsets [0]-[4] are the CA build's defaults; fixes_init overwrites them from
 * the selected fw_profiles[] entry.
 *
 * [0] amdv_parse_metadata_v2_stb      (el_enable_pre)  enable_mel check, paired BL frames
 * [1] amdv_parse_metadata_v2_stb      (meta_observe)   EL flag for paired frames
 * [2] dvel_receiver_event_fun         (branch_pre)     receiver event, unreg path
 * [3] dvel_receiver_event_fun         (branch_pre)     receiver event, reg path
 * [4] dv_toggle_frame                 (peek_pre)       dveldec provider peek
 * [5] set_layer_display_canvas        (canvas_pre) — entry point, no offset needed
 * [6] VSYNC_WR_DV_REG                 (reg_pre)   — entry point, no offset needed
 * [7] VSYNC_WR_DV_REG_BITS            (vd2_bits_pre) — entry point, no offset needed
 */
static struct kprobe fix_probes[]={
 {.symbol_name="amdv_parse_metadata_v2_stb",.offset=0x1420,.pre_handler=el_enable_pre},  /* [0] cmp w8,#0 right after ldrb enable_mel (0x289e54); x8=1 for paired BL. SC2 0xf20 */
 {.symbol_name="amdv_parse_metadata_v2_stb",.offset=0x1a0,.pre_handler=meta_observe},    /* [1] merge point after aux fetch (0x288bd4). The old +0x54c was only reached for MEL. SC2 0x184 */
 {.symbol_name="dvel_receiver_event_fun",.offset=0x44,.pre_handler=branch_pre},         /* [2] cbnz w8 (multi_dv_mode), event 1/unreg path; UNVERIFIED. SC2 0x70 */
 {.symbol_name="dvel_receiver_event_fun",.offset=0x6c,.pre_handler=branch_pre},         /* [3] cbnz w8 (multi_dv_mode), event 8/reg path; UNVERIFIED. SC2 0x180 */
 {.symbol_name="dv_toggle_frame",.offset=0x50,.pre_handler=peek_pre},                   /* [4] cbnz w8 (multi_dv_mode) skipping amdv_vf_peek_el (0xfbb94). SC2 0x68 */
 {.symbol_name="set_layer_display_canvas",.pre_handler=canvas_pre},
 {.symbol_name="VSYNC_WR_DV_REG",.pre_handler=reg_pre},
 {.symbol_name="VSYNC_WR_DV_REG_BITS",.pre_handler=vd2_bits_pre},
};

/*
 * LLDV for displays without Dolby Vision: the DV core still tone maps to the forced
 * VSVDB target, and the HDMI signalling is replaced with HDR10 after the stock driver
 * programs the low-latency output. Off unless lldv_hdr10 is set.
 */
#define HDMITX21_SUFFIX ".17779c1af08c73f32d908178815cddfb"
static void (*lldv_drm_fn)(struct master_display_info_s *data);
static void (*lldv_vend_fn)(u8 *hb,u8 *pb);
static struct kretprobe lldv_probe;
static bool lldv_probe_active;

static void __nocfi lldv_signal_hdr10(void)
{
 struct master_display_info_s info={
  .present_flag=1,
  .features=(1U<<29)|(5U<<26)|(1U<<24)|(9U<<16)|(16U<<8)|9U,
  .primaries={{8500,39850},{6550,2300},{35400,14600}},
  .white_point={15635,16450},
 };
 u32 peak=READ_ONCE(lldv_peak_nits),black=50,cll=peak,fall=peak;
 u32 limit=READ_ONCE(lldv_limit_nits);
 if(READ_ONCE(lldv_meta)==1&&READ_ONCE(lldv_src_max_lum)){
  peak=READ_ONCE(lldv_src_max_lum);black=READ_ONCE(lldv_src_min_lum);
  cll=READ_ONCE(lldv_src_cll);fall=READ_ONCE(lldv_src_fall);
 }
 if(limit){peak=min(peak,limit);cll=min(cll,limit);fall=min(fall,limit);}
 info.luminance[0]=peak;info.luminance[1]=black;
 info.max_content=cll;info.max_frame_average=fall;
 lldv_vend_fn(NULL,NULL);
 lldv_drm_fn(&info);
 lldv_sent++;
}

static int lldv_entry(struct kretprobe_instance *ri,struct pt_regs *r)
{
 *(u32 *)ri->data=(u32)r->regs[0];
 return 0;
}

static int lldv_ret(struct kretprobe_instance *ri,struct pt_regs *r)
{
 if(*(u32 *)ri->data!=EOTF_T_LL_MODE)return 0;
 lldv_ll_seen++;
 if(READ_ONCE(lldv_hdr10))lldv_signal_hdr10();
 return 0;
}

/* Same approach as avdvplus: hold the link format across every mode change. */
#define LLDV_CS_YUV422 1
#define LLDV_CD_36B 6
static int lldv_format_pre(struct kprobe *p,struct pt_regs *r)
{
 if(READ_ONCE(lldv_force_422)){r->regs[3]=LLDV_CS_YUV422;r->regs[4]=LLDV_CD_36B;}
 return 0;
}
static struct kprobe lldv_format_probe={
 .symbol_name="hdmitx_format_para_init",.pre_handler=lldv_format_pre,
};
static bool lldv_format_probe_active;

static int lldv_init(void)
{
 int ret;
 lldv_drm_fn=(void *)lookup_name("hdmitx_set_drm_pkt" HDMITX21_SUFFIX);
 lldv_vend_fn=(void *)lookup_name("hdmi_vend_infoframe_rawset");
 if(!lldv_drm_fn||!lldv_vend_fn){pr_err("dvfel: HDMI packet functions not found\n");return -ENODEV;}
 lldv_probe.kp.symbol_name="hdmitx_set_vsif_pkt" HDMITX21_SUFFIX;
 lldv_probe.entry_handler=lldv_entry;lldv_probe.handler=lldv_ret;
 lldv_probe.data_size=sizeof(u32);lldv_probe.maxactive=8;
 ret=register_kretprobe(&lldv_probe);
 if(ret){pr_err("dvfel: LLDV probe failed %d\n",ret);return ret;}
 lldv_probe_active=true;
 ret=register_kprobe(&lldv_format_probe);
 if(ret){pr_err("dvfel: LLDV format probe failed %d\n",ret);return ret;}
 lldv_format_probe_active=true;
 return 0;
}

static struct kretprobe bl_detach_probe;
static bool bl_detach_probe_active;
static int bl_detach_entry(struct kretprobe_instance *ri,struct pt_regs *r)
{
 const char *name=NULL;char text[16]={0};
 *(bool *)ri->data=false;
 if((READ_ONCE(fix_session)||READ_ONCE(g.closing)) && r->regs[2] &&
    !copy_from_kernel_nofault(&name,(void *)r->regs[2],sizeof(name)) && name &&
    !copy_from_kernel_nofault(text,name,sizeof(text)-1) && !strcmp(text,"video_render.0")){
  if(r->regs[0]==1)*(bool *)ri->data=true;
  else if(r->regs[0]==8)WRITE_ONCE(g.bl_detached,false);
 }
 return 0;
}
static int bl_detach_ret(struct kretprobe_instance *ri,struct pt_regs *r)
{
 if(*(bool *)ri->data){
  WRITE_ONCE(g.bl_detached,true);
  if(READ_ONCE(g.closing))schedule_delayed_work(&g.orphan_work,msecs_to_jiffies(100));
 }
 return 0;
}
static int nfix;

static bool blank_seek_gaps;
module_param(blank_seek_gaps,bool,0644);
MODULE_PARM_DESC(blank_seek_gaps,"Video-only black interval during incomplete FEL seek/start frames; default off");
static void gap_arm_locked(bool restart)
{
 if (!g.gap_pending || restart) {
  g.gap_pending=true;
  g.gap_good_frames=0;
  g.gap_deadline=jiffies+msecs_to_jiffies(750);
  g.gap_ready_after=g.gap_deadline;
  g.gap_epochs++;
 }
}

static void restore_drop(void);
static void fixes_exit(void)
{
 cancel_delayed_work_sync(&restore_work);
 if(READ_ONCE(restore_pending))restore_drop();
 if(lldv_format_probe_active){unregister_kprobe(&lldv_format_probe);lldv_format_probe_active=false;}
 if(lldv_probe_active){unregister_kretprobe(&lldv_probe);lldv_probe_active=false;}
 if(bl_detach_probe_active){unregister_kretprobe(&bl_detach_probe);bl_detach_probe_active=false;}
 if(residual_probe_active){unregister_kprobe(&residual_probe);residual_probe_active=false;}
 if(stock_dovi_owner){module_put(stock_dovi_owner);stock_dovi_owner=NULL;}
 while(nfix)unregister_kprobe(&fix_probes[--nfix]);
}

/* kallsyms_lookup_name is not exported on 5.15; a kprobe resolves the name. */
static unsigned long lookup_name(const char *name)
{
 struct kprobe kp={.symbol_name=name};
 unsigned long addr;
 if(register_kprobe(&kp))return 0;
 addr=(unsigned long)kp.addr;
 unregister_kprobe(&kp);
 return addr;
}

static bool fw_sites_match(const struct fw_profile *p)
{
 size_t i;
 for(i=0;i<p->nsites;i++){
  unsigned long addr=lookup_name(p->sites[i].sym);
  u32 insn;
  if(!addr||copy_from_kernel_nofault(&insn,(void *)(addr+p->sites[i].off),4)||
     insn!=p->sites[i].insn)return false;
 }
 return true;
}

/* Returns the address an adrp + add/ldrb/ldr pair refers to, 0 if it is not one. */
static unsigned long fw_decode(const struct fw_ref *r)
{
 unsigned long sym=lookup_name(r->sym),pc,page;
 unsigned int off;
 u32 hi,lo;
 if(!sym)return 0;
 pc=sym+r->adrp;
 if(copy_from_kernel_nofault(&hi,(void *)pc,4)||
    copy_from_kernel_nofault(&lo,(void *)(sym+r->lo),4))return 0;
 if((hi&0x9f000000)!=0x90000000||(hi&0x1f)!=((lo>>5)&0x1f))return 0;
 page=(pc&~0xfffUL)+
      (unsigned long)(sign_extend64(((hi>>29)&3)|(((hi>>5)&0x7ffff)<<2),20)*4096);
 off=(lo>>10)&0xfff;
 switch(lo&0xffc00000){
 case 0x91000000:case 0x39400000:break;  /* add, ldrb */
 case 0xb9400000:off<<=2;break;          /* ldr w */
 default:return 0;
 }
 return page+off;
}

static int fw_resolve_data(void)
{
 fw_vd_layer=fw_decode(&fw->vd_layer);
 fw_enable_fel=fw_decode(&fw->enable_fel);
 fw_el_flags=fw_decode(&fw->el_flags);
 fw_new_el_flags=fw_decode(&fw->new_el_flags);
 fw_amdv_mask=fw_decode(&fw->amdv_mask);
 fw_dv_flags=fw_decode(&fw->dv_flags);
 if(!fw_vd_layer||!fw_enable_fel||!fw_el_flags||!fw_new_el_flags||!fw_amdv_mask||!fw_dv_flags){
  pr_err("dvfel: could not resolve driver data addresses\n");
  return -ENODEV;
 }
 return 0;
}

static int fw_select(void)
{
 unsigned long anchor=lookup_name("READ_VPP_DV_REG");
 size_t i;
 if(!anchor)return -ENODEV;
 for(i=0;i<ARRAY_SIZE(fw_profiles);i++){
  const struct fw_profile *p=&fw_profiles[i];
  if(strcmp(init_uts_ns.name.release,p->release)||!fw_sites_match(p))continue;
  fw=p;fw_text=anchor-p->read_reg;
  pr_info("dvfel: firmware profile: %s\n",p->name);
  return 0;
 }
 pr_err("dvfel: no profile for this firmware (%s), refusing to load\n",
        init_uts_ns.name.release);
 return -ENODEV;
}

static int fixes_init(void)
{
 int i,ret;
 ret=fw_select();if(ret)return ret;

 read_dv_reg_fn=(void *)lookup_name("READ_VPP_DV_REG");
 write_dv_bits_fn=(void *)lookup_name("VSYNC_WR_DV_REG_BITS");
 write_dv_reg_fn=(void *)lookup_name("VSYNC_WR_DV_REG");
 write_dv_bits_now_fn=(void *)lookup_name("WRITE_VPP_DV_REG_BITS");

 /* Verify function addresses match the selected profile */
 if((unsigned long)read_dv_reg_fn!=FW_ADDR(read_reg) ||
    (unsigned long)write_dv_bits_fn!=FW_ADDR(write_bits) ||
    (unsigned long)write_dv_bits_now_fn!=FW_ADDR(write_bits_now) ||
    !write_dv_reg_fn)return -ENODEV;

 /* Guard hashes come from the unrelocated .ko and cannot match runtime bytes,
  * so check each symbol's position relative to the text base instead. */
 for(i=0;i<fw->nguards;i++){
  if(lookup_name(fw->guards[i].name)!=fw->guards[i].addr-fw->link_base+fw_text){
   pr_err("dvfel: symbol address mismatch: %s\n",fw->guards[i].name);
   return -EINVAL;
  }
 }
 pr_info("dvfel: symbol addresses verified (code guard hashes skipped)\n");

 ret=fw_resolve_data();if(ret)return ret;

 fix_probes[0].offset=fw->el_enable_off;
 fix_probes[1].offset=fw->meta_off;
 fix_probes[2].offset=fw->recv_off[0];
 fix_probes[3].offset=fw->recv_off[1];
 fix_probes[4].offset=fw->peek_off;

 for(i=0;i<ARRAY_SIZE(fix_probes);i++){
  /* Guard: a profile with a missing offset skips that probe. */
  if(fix_probes[i].offset==0 && i<5){
   pr_warn("dvfel: skipping probe %d (%s+0x%lx) — offset not yet determined\n",
           i, fix_probes[i].symbol_name, fix_probes[i].offset);
   continue;
  }
  ret=register_kprobe(&fix_probes[i]);
  if(ret){pr_err("dvfel: fix probe %d failed %d\n",i,ret);fixes_exit();return ret;}
  nfix++;
 }

 bl_detach_probe.kp.symbol_name="common_receiver_event_fun";
 bl_detach_probe.entry_handler=bl_detach_entry;bl_detach_probe.handler=bl_detach_ret;
 bl_detach_probe.data_size=sizeof(bool);bl_detach_probe.maxactive=8;
 ret=register_kretprobe(&bl_detach_probe);if(ret){fixes_exit();return ret;}
 bl_detach_probe_active=true;
 ret=lldv_init();if(ret){fixes_exit();return ret;}

 ret=residual_init();if(ret){fixes_exit();return ret;}
 pr_info("dvfel: ONN S7D layout verified, reversible fixes idle\n");
 return 0;
}

static void fixes_start(void)
{
 WRITE_ONCE(tracked_composer_mode,-1);
 /* A restart before the last stop's RDMA copies went out: the direct writes at stop
  * already put the stock values back, so the registers read below are stock. */
 if(READ_ONCE(restore_pending)){restore_drop();restore_skipped++;}
 pr_info("dvfel: session start, %d lib regs carried\n",lib_nregs);
 saved_enable_fel=READ_ONCE(*(u8 *)FEL_ENABLE_ADDR);
 saved_el_mux=(read_dv_reg(0x1a71)>>4)&3;
 saved_vd2_bypass=(read_dv_reg(0x1a84)>>4)&1;
 mux_allowed=((read_dv_reg(0x1a70)>>4)&7)==0 && saved_el_mux==3;
 mux_changed=false;composer_mode_changed=false;vd2_changed=false;path_changed=false;
 WRITE_ONCE(comp_ev_n,0);comp_live1=0;comp_live0=0;WRITE_ONCE(comp_seen_el,false);
 pr_info("dvfel: S7D EL mux saved=%u route_allowed=%d\n",saved_el_mux,mux_allowed);
 WRITE_ONCE(fix_session,true);
 WRITE_ONCE(*(u8 *)FEL_ENABLE_ADDR,1);
}

/* (vsync path) Puts back, as RDMA writes for the next vsync: the stock EL routing, the
 * composer mode, and the library's own value of every core register reg_pre changed during
 * the session. Clears the *_changed flags and the table. */
static void fixes_restore_regs(void)
{
 int i;
 if(path_changed){write_dv_bits(0x1a0c,1,1,1);path_changed=false;}
 if(vd2_changed){write_dv_bits(0x1a84,saved_vd2_bypass,4,1);vd2_changed=false;}
 if(mux_changed){write_dv_bits(0x1a71,saved_el_mux,4,2);mux_changed=false;}
 /* bit writes don't stick on the composer mode register: full write, as the library does */
 if(composer_mode_changed){write_dv_reg(0x01000032,1);composer_mode_changed=false;}
 for(i=0;i<lib_nregs;i++)write_dv_reg(lib_regs[i].reg,lib_regs[i].lib);
 restore_libregs+=lib_nregs;lib_nregs=0;
}

/* A new session before the restore: the direct writes at stop put the routing back, so only
 * drop those flags. The library-value table stays (this session keeps changing the same
 * registers; its stop restores them). */
static void restore_drop(void)
{
 WRITE_ONCE(restore_pending,false);
 path_changed=vd2_changed=mux_changed=composer_mode_changed=false;
}

static void restore_work_fn(struct work_struct *w) { }

/*
 * Session end. The stock EL routing and composer mode are written directly at once. The rest
 * waits for the next DV core write in the vsync path (reg_pre → fixes_restore_regs), before the
 * library writes anything for the next frame: RDMA writes from this ioctl/binder thread raced
 * the vsync (rdma_write warnings). The library only rewrites registers whose value it thinks
 * changed (it keeps its own copy), so any core register reg_pre changed would otherwise keep
 * the FEL value into the next play: a non-FEL play then composed green garbage until the
 * library happened to rewrite it (seen up to 2026.11.20.8).
 */
static void fixes_stop(void)
{
 WRITE_ONCE(tracked_composer_mode,-1);
 WRITE_ONCE(*(u8 *)FEL_ENABLE_ADDR,saved_enable_fel);
 WRITE_ONCE(fix_session,false);
 if(path_changed)write_dv_bits_now(0x1a0c,1,1,1);
 if(vd2_changed)write_dv_bits_now(0x1a84,saved_vd2_bypass,4,1);
 if(mux_changed)write_dv_bits_now(0x1a71,saved_el_mux,4,2);
 if(composer_mode_changed)write_dv_bits_now(0x01000032,1,0,1);
 if(path_changed||vd2_changed||mux_changed||composer_mode_changed||lib_nregs)
  WRITE_ONCE(restore_pending,true);
  pr_info("dvfel: session stop, restore pending: %d lib regs\n",lib_nregs);
}
