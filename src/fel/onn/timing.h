/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Diagnostic timing of the video vsync path, for display-side frame loss.
 * Off until "1" is written to /sys/kernel/debug/dvfel/timing ("0" = off, "reset" = clear).
 * Reading it shows, per probed function: calls, average and maximum time, and how many
 * calls took longer than 1/4/8/16 ms, split into FEL-session and other calls. For
 * vsync_isr it also shows the gaps between handlers (a gap of about two periods is a
 * vsync with no handler) and the Dolby Vision register writes made per vsync.
 */
struct tstat { u64 n, sum, max, over[4]; };
struct tprobe {
	const char *sym;
	struct kretprobe krp;
	bool on;
	struct tstat fel, other;
};
static const u64 t_lim[4] = { 1000000, 4000000, 8000000, 16000000 };

static struct tprobe tprobes[] = {
	{ .sym = "vsync_isr" },
	{ .sym = "dv_toggle_frame" },
	{ .sym = "amdv_parse_metadata" },
	{ .sym = "amdolby_vision_process" },
	{ .sym = "dv_core1a_set" },
};
#define T_VSYNC 0

/* vsync_isr extras */
static u64 t_vs_last;
static u64 t_gap[6];		/* <20, 20-30, 30-45, 45-60, 60-90, >=90 ms */
static u64 t_gap_max;
static struct tstat t_regs_fel, t_regs_other;	/* DV register writes per vsync (as "ns" fields) */
static DEFINE_MUTEX(t_lock);

struct tdata { u64 t0; u64 regs0; };

static void tstat_add(struct tstat *s, u64 v, bool lim)
{
	int i;
	s->n++; s->sum += v; if (v > s->max) s->max = v;
	if (lim)
		for (i = 0; i < 4; i++)
			if (v > t_lim[i]) s->over[i]++;
}

static int t_entry(struct kretprobe_instance *ri, struct pt_regs *r)
{
	struct tdata *d = (struct tdata *)ri->data;
	struct tprobe *p = container_of(get_kretprobe(ri), struct tprobe, krp);

	d->t0 = ktime_get_ns();
	d->regs0 = READ_ONCE(dvreg_writes);
	if (p == &tprobes[T_VSYNC]) {
		u64 last = t_vs_last, gap;
		t_vs_last = d->t0;
		if (last) {
			gap = d->t0 - last;
			if (gap > t_gap_max) t_gap_max = gap;
			t_gap[gap < 20000000 ? 0 : gap < 30000000 ? 1 : gap < 45000000 ? 2 :
			      gap < 60000000 ? 3 : gap < 90000000 ? 4 : 5]++;
		}
	}
	return 0;
}

static int t_ret(struct kretprobe_instance *ri, struct pt_regs *r)
{
	struct tdata *d = (struct tdata *)ri->data;
	struct tprobe *p = container_of(get_kretprobe(ri), struct tprobe, krp);
	bool fel = READ_ONCE(fix_session);
	u64 dt = ktime_get_ns() - d->t0;

	tstat_add(fel ? &p->fel : &p->other, dt, true);
	if (p == &tprobes[T_VSYNC])
		tstat_add(fel ? &t_regs_fel : &t_regs_other, READ_ONCE(dvreg_writes) - d->regs0, false);
	return 0;
}

static void timing_off(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(tprobes); i++)
		if (tprobes[i].on) { unregister_kretprobe(&tprobes[i].krp); tprobes[i].on = false; }
}

static void timing_reset(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(tprobes); i++) {
		memset(&tprobes[i].fel, 0, sizeof(struct tstat));
		memset(&tprobes[i].other, 0, sizeof(struct tstat));
	}
	memset(t_gap, 0, sizeof(t_gap)); t_gap_max = 0; t_vs_last = 0;
	memset(&t_regs_fel, 0, sizeof(t_regs_fel)); memset(&t_regs_other, 0, sizeof(t_regs_other));
}

static void timing_on(void)
{
	int i, ret;
	for (i = 0; i < ARRAY_SIZE(tprobes); i++) {
		struct tprobe *p = &tprobes[i];
		if (p->on) continue;
		memset(&p->krp, 0, sizeof(p->krp));
		p->krp.kp.symbol_name = p->sym;
		p->krp.entry_handler = t_entry;
		p->krp.handler = t_ret;
		p->krp.data_size = sizeof(struct tdata);
		p->krp.maxactive = 8;
		ret = register_kretprobe(&p->krp);
		if (ret) pr_info("dvfel: timing probe %s not set (%d)\n", p->sym, ret);
		else p->on = true;
	}
}

static void tstat_show(struct seq_file *m, const char *name, const char *kind, struct tstat *s)
{
	if (!s->n) return;
	seq_printf(m, "%-24s %-5s n %llu avg %llu.%03llu max %llu.%03llu ms  >1ms %llu >4ms %llu >8ms %llu >16ms %llu\n",
		   name, kind, s->n, div64_u64(s->sum, s->n) / 1000000, div64_u64(s->sum, s->n) / 1000 % 1000,
		   s->max / 1000000, s->max / 1000 % 1000, s->over[0], s->over[1], s->over[2], s->over[3]);
}

static int timing_show(struct seq_file *m, void *v)
{
	int i;
	seq_printf(m, "timing probes:");
	for (i = 0; i < ARRAY_SIZE(tprobes); i++)
		seq_printf(m, " %s=%d", tprobes[i].sym, tprobes[i].on);
	seq_putc(m, '\n');
	for (i = 0; i < ARRAY_SIZE(tprobes); i++) {
		tstat_show(m, tprobes[i].sym, "fel", &tprobes[i].fel);
		tstat_show(m, tprobes[i].sym, "other", &tprobes[i].other);
	}
	seq_printf(m, "vsync gaps <20 %llu 20-30 %llu 30-45 %llu 45-60 %llu 60-90 %llu >=90 %llu ms, max %llu.%03llu ms\n",
		   t_gap[0], t_gap[1], t_gap[2], t_gap[3], t_gap[4], t_gap[5], t_gap_max / 1000000, t_gap_max / 1000 % 1000);
	if (t_regs_fel.n)
		seq_printf(m, "dv reg writes per vsync fel   avg %llu max %llu\n", div64_u64(t_regs_fel.sum, t_regs_fel.n), t_regs_fel.max);
	if (t_regs_other.n)
		seq_printf(m, "dv reg writes per vsync other avg %llu max %llu\n", div64_u64(t_regs_other.sum, t_regs_other.n), t_regs_other.max);
	return 0;
}

static int timing_open(struct inode *i, struct file *f) { return single_open(f, timing_show, NULL); }

static ssize_t timing_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	char b[8] = { 0 };
	if (copy_from_user(b, ubuf, min(len, sizeof(b) - 1)))
		return -EFAULT;
	mutex_lock(&t_lock);
	if (b[0] == '1') { timing_reset(); timing_on(); }
	else if (b[0] == '0') timing_off();
	else if (b[0] == 'r') timing_reset();
	mutex_unlock(&t_lock);
	return len;
}

static const struct file_operations timing_fops = {
	.owner = THIS_MODULE, .open = timing_open, .read = seq_read,
	.llseek = seq_lseek, .release = single_release, .write = timing_write,
};
