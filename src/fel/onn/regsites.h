/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The core register hook (reg_pre) on the call sites instead of VSYNC_WR_DV_REG's entry.
 *
 * In a FEL play the DV core code calls VSYNC_WR_DV_REG ~1,450 times per vsync, ~1,150 of them
 * for the core1a table (data port 0x..0000ff). A kprobe hit costs about 1 us here, so the entry
 * hook added ~1.5 ms to every vsync handler. With display-led output that pushed the handler
 * past SurfaceFlinger's 4 ms and frames were dropped at 24 Hz (measured on .111, 2026-11-26).
 *
 * reg_pre only changes core registers 1, 0xf3 and 0x32 (prefixes 0x01/0x02/0x20 000000).
 * At load, the code between apply_stb_core_settings and the end of dv_core1b_set is scanned
 * for "bl VSYNC_WR_DV_REG". A call whose register is a constant (movz/movk w0 just before) is
 * hooked only if that constant is one of ours; every call with a computed register is hooked.
 * Each hook runs reg_pre with the arguments already in x0/x1 and lr = the call's return address.
 */

#define RS_MAX 256
static struct kprobe rs_probes[RS_MAX];
static u32 rs_reg[RS_MAX];		/* constant register, 0 = computed */
static u64 rs_hits[RS_MAX];
static u32 rs_min[RS_MAX], rs_max[RS_MAX];	/* registers this site wrote */
static bool rs_ours[RS_MAX];			/* it wrote one of ours */
static bool rs_dropped[RS_MAX];
static int rs_n, rs_skipped_const, rs_calls, rs_ndropped;
static bool rs_active;
static void rs_learn_fn(struct work_struct *w);
static DECLARE_DELAYED_WORK(rs_learn, rs_learn_fn);

static bool reg_sites = true;
module_param(reg_sites, bool, 0444);
MODULE_PARM_DESC(reg_sites, "Hook the core register writes at their call sites (cheap) instead of VSYNC_WR_DV_REG's entry");

static bool rs_target(u32 reg)
{
	u32 hi = reg & 0xff000000, off = reg & 0xffffff;

	if (hi != 0x01000000 && hi != 0x02000000 && hi != 0x20000000)
		return false;
	return off == 1 || off == 3 || off == 0xf3 || (off == 0x32 && hi == 0x01000000);
}

static int rs_pre(struct kprobe *p, struct pt_regs *r)
{
	unsigned long lr = r->regs[30];
	int i = (int)(p - rs_probes);

	if (i >= 0 && i < RS_MAX) {
		u32 reg = (u32)r->regs[0];

		if (!rs_hits[i]++) {
			rs_min[i] = rs_max[i] = reg;
		} else {
			if (reg < rs_min[i]) rs_min[i] = reg;
			if (reg > rs_max[i]) rs_max[i] = reg;
		}
		if (rs_target(reg))
			rs_ours[i] = true;
	}
	r->regs[30] = (unsigned long)p->addr + 4;
	reg_pre(p, r);
	r->regs[30] = lr;
	return 0;
}

/* the constant w0 set by movz/movk in the few instructions before the call, or 0 */
static u32 rs_const_w0(const u32 *pc)
{
	u32 lo = 0, hi = 0;
	bool have_lo = false, have_hi = false;
	int k;

	for (k = 1; k <= 6; k++) {
		u32 in = pc[-k];

		if ((in & 0xffe0001f) == 0x52800000) {		/* movz w0, #imm16 */
			lo = (in >> 5) & 0xffff;
			have_lo = true;
			break;
		}
		if ((in & 0xffe0001f) == 0x72a00000) {		/* movk w0, #imm16, lsl #16 */
			hi = (in >> 5) & 0xffff;
			have_hi = true;
			continue;
		}
		if ((in & 0x1f) == 0 && (in & 0x1f000000) != 0x14000000 &&
		    ((in >> 24) & 0xff) != 0xb9 && ((in >> 24) & 0xff) != 0xf9 &&
		    ((in >> 24) & 0xff) != 0x29 && ((in >> 24) & 0xff) != 0xa9)
			return 0;	/* something else writes w0/x0: computed (stores excluded) */
		if ((in & 0xfc000000) == 0x14000000 || (in & 0xfc000000) == 0x94000000 ||
		    (in & 0xff000010) == 0x54000000 || (in & 0x7e000000) == 0x34000000 ||
		    (in & 0x7e000000) == 0x36000000 || in == 0xd65f03c0)
			return 0;	/* a branch: the value may come from elsewhere */
	}
	return have_lo ? ((have_hi ? hi << 16 : 0) | lo) : 0;
}

/* any of our registers between lo and hi */
static bool rs_range_has_target(u32 lo, u32 hi)
{
	static const u32 pre[] = { 0x01000000, 0x02000000, 0x20000000 };
	static const u32 off[] = { 1, 3, 0xf3, 0x32 };
	int a, b;

	for (a = 0; a < 3; a++)
		for (b = 0; b < 4; b++) {
			u32 t = pre[a] | off[b];

			if (rs_target(t) && t >= lo && t <= hi)
				return true;
		}
	return false;
}

/*
 * Every 2 s: a site that has written 1000+ times, never one of our registers and no range that
 * could hold one (a table loop such as the 1,280 writes per vsync to 0x0400003f) loses its hook.
 */
static void rs_learn_fn(struct work_struct *w)
{
	int i, n = 0;

	for (i = 0; i < rs_n; i++) {
		if (rs_dropped[i] || READ_ONCE(rs_hits[i]) < 1000 || READ_ONCE(rs_ours[i]) ||
		    rs_range_has_target(rs_min[i], rs_max[i]))
			continue;
		unregister_kprobe(&rs_probes[i]);
		rs_dropped[i] = true;
		rs_ndropped++;
		n++;
		pr_info("dvfel: register call site +%lx (writes %x..%x, %llu hits) unhooked\n",
			(unsigned long)rs_probes[i].addr - FW_ADDR(apply_core), rs_min[i], rs_max[i], rs_hits[i]);
	}
	if (rs_active)
		schedule_delayed_work(&rs_learn, 2 * HZ);
}

static int rs_init(void)
{
	const u32 *pc = (const u32 *)FW_ADDR(apply_core), *end = (const u32 *)FW_ADDR(core1b_end);
	unsigned long target = (unsigned long)write_dv_reg_fn;
	int ret;

	if (!target || !READ_ONCE(reg_sites))
		return -ENOENT;
	for (; pc < end; pc++) {
		u32 in = *pc, reg;
		long off;

		if ((in & 0xfc000000) != 0x94000000)
			continue;
		off = (long)((s32)(in << 6) >> 6) * 4;	/* imm26, sign-extended */
		if ((unsigned long)pc + off != target)
			continue;
		rs_calls++;
		reg = rs_const_w0(pc);
		if (reg && !rs_target(reg)) {
			rs_skipped_const++;
			continue;
		}
		if (rs_n == RS_MAX) {
			pr_warn("dvfel: more than %d register call sites, using the entry hook\n", RS_MAX);
			goto undo;
		}
		rs_probes[rs_n].addr = (kprobe_opcode_t *)pc;
		rs_probes[rs_n].pre_handler = rs_pre;
		rs_reg[rs_n] = reg;
		ret = register_kprobe(&rs_probes[rs_n]);
		if (ret) {
			pr_warn("dvfel: register call site %px: %d, using the entry hook\n", pc, ret);
			goto undo;
		}
		rs_n++;
	}
	if (!rs_calls) {
		pr_warn("dvfel: no VSYNC_WR_DV_REG calls found in the core code, using the entry hook\n");
		return -ENOENT;
	}
	rs_active = true;
	schedule_delayed_work(&rs_learn, 2 * HZ);
	pr_info("dvfel: core register hook on %d of %d call sites (%d constant, not ours)\n",
		rs_n, rs_calls, rs_skipped_const);
	return 0;
undo:
	while (rs_n)
		unregister_kprobe(&rs_probes[--rs_n]);
	return -EINVAL;
}

static void rs_exit(void)
{
	rs_active = false;
	cancel_delayed_work_sync(&rs_learn);
	while (rs_n) {
		rs_n--;
		if (!rs_dropped[rs_n])
			unregister_kprobe(&rs_probes[rs_n]);
	}
}

static void rs_show(struct seq_file *m)
{
	u64 tot = 0;
	int i;

	seq_printf(m, "reg_sites %d active %d hooked %d of %d calls, skipped %d, unhooked after learning %d\n",
		   reg_sites, rs_active, rs_n - rs_ndropped, rs_calls, rs_skipped_const, rs_ndropped);
	for (i = 0; i < rs_n; i++)
		tot += rs_hits[i];
	seq_printf(m, "reg_sites hits %llu:", tot);
	for (i = 0; i < rs_n; i++)
		if (rs_hits[i] > 1000 && !rs_dropped[i])
			seq_printf(m, " +%lx/%x=%llu", (unsigned long)rs_probes[i].addr - FW_ADDR(apply_core),
				   rs_reg[i], rs_hits[i]);
	seq_puts(m, "\n");
}
