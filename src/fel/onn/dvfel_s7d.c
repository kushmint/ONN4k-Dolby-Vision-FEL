// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dvfel (S7D) - Dolby Vision FEL composition by the Dolby Vision core of an
 * Amlogic S905X5M (S7D) on Android TV 14 firmwares with the 5.15.196 kernel
 * (ONN 4K Pro v2, codename jarvis2).
 *
 * Ported from signde's Fire TV Cube 3 (SC2/PS7702) module:
 *   https://github.com/signde/firetv-passthrough
 *
 * The SoC composes BL + EL itself when the Dolby Vision driver gets the EL
 * picture of each frame from the "dveldec" provider (vfm: dveldec -> dvel), as
 * in the stream mode dual layer decoder. Android players decode with
 * MediaCodec (frame mode), which outputs the base layer only, so:
 *
 *   - the player reports every base layer access unit it queues (packet pts +
 *     RPU CRC, DVFEL_IOC_SC2_BL_HINT), decodes the EL with a second MediaCodec
 *     into an AImageReader and queues each picture here with the pts of its
 *     frame (DVFEL_IOC_SC2_EL_QUEUE: uvm dma-buf); dvfel registers as
 *     "dveldec" and passes the decoder's own vframe of that picture on;
 *   - kprobes on is_dovi_dual_layer_frame() and amdv_parse_metadata()
 *     see each base layer frame the driver processes;
 *   - Dolby Vision FEL is switched on in the driver (enable_fel = 1).
 *
 * Key S7D differences from SC2:
 *   - Kernel 5.15.196 (vs 5.4.125) — CFI enabled, different code layout
 *   - DV HW5 path: amdv_wait_metadata_hw5 (vs amdv_wait_metadata)
 *   - DV module: dovi_s7d_5_15_stb26 (vs stock dovi on SC2)
 *   - Different function sizes and kprobe offsets throughout
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/dma-buf.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <asm/unaligned.h>
#include <linux/amlogic/media/vfm/vframe.h>
#include <linux/amlogic/media/vfm/vframe_provider.h>
#include <linux/amlogic/media/vfm/vframe_receiver.h>
#include <linux/amlogic/meson_uvm_core.h>

#include "dvfel_s7d_uapi.h"

#define DRV_NAME	"dvfel"
#define EL_PROV_NAME	"dveldec"
#define DV_DEBUG_PATH	"/sys/class/amdolby_vision/debug"
#define AUX_DV_SEI	0x01000000
#define MAX_EL		DVFEL_SC2_MAX_EL
#define MAX_HINTS	DVFEL_SC2_MAX_HINTS
#define DONE_RING	64
#define HINT_SEARCH	MAX_HINTS
#define DRAIN_TRIES 100

/* src_fmt offset in struct vframe_s — measured at load via clear_vframe_src_fmt() */
#define S7D_VF_SRC_FMT_OFF	-1  /* will be measured at load */

static bool open_driver_fixed;
module_param(open_driver_fixed, bool, 0444);
MODULE_PARM_DESC(open_driver_fixed, "Experimental: requires separately verified open-driver FEL fixes");

static unsigned int seek_wait_ms;
module_param(seek_wait_ms,uint,0644);
MODULE_PARM_DESC(seek_wait_ms,"Bounded EL wait after flush, 0 uses normal 50ms; max250ms");
static bool wait_el;
module_param(wait_el, bool, 0644);
MODULE_PARM_DESC(wait_el, "Bounded EL wait experiment; default off");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "1: session events, 2: every EL picture");

#define dbg(lvl, fmt, ...) \
	do { if (debug >= (lvl)) pr_info(DRV_NAME ": " fmt, ##__VA_ARGS__); } while (0)

enum el_state { EL_FREE, EL_QUEUED, EL_OUT, EL_RELEASE };

struct el_ent {
	enum el_state state;
	struct dma_buf *db;
	struct vframe_s *vf;
	struct vframe_s *bl_vf;
	u64 bl_native;
	u32 bl_crc;
	bool display_pin;
	bool v4l_ref;
	struct dma_fence *fence;
	u32 crc;
	u32 id;
	s64 pts_us;
	unsigned long out_jiffies;
	u64 sess;	/* session that sent it to the display */
};

struct bl_hint {
	s64 pts_us;
	u32 crc;
};

static struct dvfel_sc2 {
	spinlock_t lock;
	struct el_ent ent[MAX_EL];
	struct el_ent *q[MAX_EL];
	int nq;
	struct bl_hint hint[MAX_HINTS];
	int nh;
	u64 fence_ctx;
	u32 fence_seq;
	spinlock_t fence_lock;
	u32 next_id;
	u32 done[DONE_RING];
	u32 done_head, done_tail;
	wait_queue_head_t done_wq;
	struct work_struct rel_work;
	struct delayed_work orphan_work;

	struct mutex sess_lock;
	struct mutex ioctl_lock;
	u64 session_id;
	struct file *owner;
	bool active;
	bool prov_reg;
	bool closing;
	unsigned int drain_checks;
	unsigned long drain_abandoned;
	bool bl_detached;
	u64 el_retired;
	struct vframe_provider_s prov;

	struct vframe_s *bl_vf;
	bool bl_found;
	bool gap_pending;
	unsigned int gap_good_frames;
	unsigned long gap_deadline, gap_ready_after;
	u64 gap_epochs, gap_blank_checks;
	u64 bl_native;
	u32 bl_crc;
	u64 bl_reused;

	struct vframe_s *wait_vf;
	u32 wait_crc;
	u64 wait_native;
	unsigned long wait_until;
	bool wait_expired;
	u64 wait_checks, wait_timeouts, wait_ready, wait_repairs;
	unsigned long seek_wait_until;
	u64 seek_wait_extended;

	u64 el_in, el_out, el_put, el_dropped, el_flushed, bl_paired, bl_no_el, bl_no_crc, bl_no_hint;
	u64 hints_in, hints_skipped, hints_overflow;
	u64 sess_puts, adopts, stale_adopted, stale_reclaimed;
} g;

#include "live_fixes.h"
#include "timing.h"

static struct kretprobe krp_dual, krp_wait;
static struct kprobe kp_parse;
static bool krp_dual_ok, kp_parse_ok;
static struct dentry *dbg_dir;

static int src_fmt_off = -1;

static inline struct vframe_src_fmt_s *s7d_src_fmt(struct vframe_s *vf)
{
	return (struct vframe_src_fmt_s *)((u8 *)vf + src_fmt_off);
}

#define SEI_MAGIC	0x53656920	/* "Sei " */
static int measure_src_fmt_off(void)
{
	const int lo = 512, hi = 3072;
	int off = -1, k, n = 0;
	u8 *buf = kzalloc(hi + 256, GFP_KERNEL);

	if (!buf)
		return -1;
	for (k = lo; k < hi; k += 4)
		*(u32 *)(buf + k) = SEI_MAGIC;
	if (clear_vframe_src_fmt((struct vframe_s *)buf) == 0) {
		for (k = lo; k < hi - 24; k += 4) {
			if (*(u32 *)(buf + k) != 0xffffffff)
				continue;
			if (*(u64 *)(buf + k + 8) || *(u32 *)(buf + k + 16) || buf[k + 20])
				continue;
			off = k;
			n++;
		}
	}
	kfree(buf);
	return n == 1 ? off : -1;
}

static bool rpu_crc_of(struct vframe_s *vf, u32 *crc)
{
	struct vframe_src_fmt_s *sf = s7d_src_fmt(vf);
	const u8 *p = sf->sei_ptr, *end;
	u8 win[5] = { 0 }, last[5] = { 0 };
	u32 size, type, i, n = 0;
	bool have = false;
	int zeros = 0;

	if (!p || sf->sei_size < 8 || sf->sei_size > 64 * 1024)
		return false;
	end = p + sf->sei_size;
	while (end - p >= 8) {
		size = get_unaligned_be32(p);
		type = get_unaligned_be32(p + 4);
		p += 8;
		if (!size || size > end - p)
			return false;
		if (type != AUX_DV_SEI) {
			p += size;
			continue;
		}
		for (i = 0; i < size; i++) {
			u8 b = p[i];

			if (zeros >= 2 && b == 3) {
				zeros = 0;
				continue;
			}
			zeros = b ? 0 : zeros + 1;
			memmove(win, win + 1, 4);
			win[4] = b;
			n++;
			if (b && n >= 5) {
				memcpy(last, win, 5);
				have = true;
			}
		}
		if (!have || last[4] != 0x80)
			return false;
		*crc = get_unaligned_be32(last);
		return true;
	}
	return false;
}

static struct el_ent *q_remove(int i)
{
	struct el_ent *e = g.q[i];
	memmove(&g.q[i], &g.q[i + 1], (g.nq - i - 1) * sizeof(e));
	g.nq--;
	return e;
}

static void el_vframe_put(struct dma_buf *db, bool v4l_ref)
{
	if (v4l_ref) uvm_put_hook_mod(db, VF_PROCESS_V4LVIDEO);
	else dmabuf_put_vframe(db);
}

static struct vframe_s *el_vframe_get(struct dma_buf *db, bool *v4l_ref)
{
	struct uvm_hook_mod *hook;
	struct vframe_s *vf;
	u32 flag;
	BUILD_BUG_ON(offsetof(struct uvm_hook_mod, arg) != 8);
	BUILD_BUG_ON(VF_PROCESS_V4LVIDEO != 2);
	*v4l_ref = false;
	vf = dmabuf_get_vframe(db);
	if (IS_ERR(vf) || vf) return vf;
	dmabuf_put_vframe(db);
	hook = uvm_get_hook_mod(db, VF_PROCESS_V4LVIDEO);
	if (IS_ERR(hook)) return ERR_CAST(hook);
	if (!hook) return ERR_PTR(-ENOENT);
	/* file_private_data.flag: nonzero means the picture is in vf_ext, not vf */
	if (!hook->arg || copy_from_kernel_nofault(&flag, (char *)hook->arg + fw->v4l_flag_off, 4) || flag) {
		uvm_put_hook_mod(db, VF_PROCESS_V4LVIDEO);
		return ERR_PTR(-EINVAL);
	}
	*v4l_ref = true;
	return (struct vframe_s *)hook->arg;
}

static void ent_release_locked(struct el_ent *e)
{
	e->state = EL_RELEASE;
	schedule_work(&g.rel_work);
}

/*
 * retire_stopped_frames: reclaim EL frames after BL path detaches.
 *
 * Stub on S7D: the SC2 version reads DV driver internals (unreg / inirq counters,
 * dvlock, current frame, EL pair array) that have not been located on S7D.
 * Known effect: one EL frame stays held after a play, so dvfel cannot unload.
 */
static bool retire_stopped_frames(void)
{
	pr_info_once("dvfel: retire_stopped_frames not yet ported for S7D\n");
	return false;
}

static void rel_work_fn(struct work_struct *work)
{
	struct el_ent *rel[MAX_EL];
	unsigned long flags;
	int i, n = 0;

	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MAX_EL; i++) {
		struct el_ent *e = &g.ent[i];
		if (e->state != EL_RELEASE)
			continue;
		rel[n++] = e;
	}
	spin_unlock_irqrestore(&g.lock, flags);

	for (i = 0; i < n; i++) {
		struct el_ent *e = rel[i];
		el_vframe_put(e->db, e->v4l_ref);
		dma_buf_put(e->db);
		if (e->fence) {
			dma_fence_signal(e->fence);
			dma_fence_put(e->fence);
			e->fence = NULL;
		}
		spin_lock_irqsave(&g.lock, flags);
		if (g.done_head - g.done_tail < DONE_RING)
			g.done[g.done_head++ % DONE_RING] = e->id;
		e->db = NULL;
		e->vf = NULL;
		e->state = EL_FREE;
		if(e->display_pin){e->display_pin=false;module_put(THIS_MODULE);}
		spin_unlock_irqrestore(&g.lock, flags);
	}
	if (n)
		wake_up_interruptible(&g.done_wq);
}

static bool pts_of_crc(u32 crc, s64 *pts)
{
	int i, n = min(g.nh, HINT_SEARCH);
	for (i = 0; i < n; i++) {
		if (g.hint[i].crc != crc)
			continue;
		*pts = g.hint[i].pts_us;
		g.hints_skipped += i;
		memmove(&g.hint[0], &g.hint[i + 1], (g.nh - i - 1) * sizeof(g.hint[0]));
		g.nh -= i + 1;
		return true;
	}
	return false;
}

static bool paired_owned_locked(struct vframe_s *vf, u64 native, u32 crc)
{
	int i;
	for (i=0; i<MAX_EL; i++) {
		struct el_ent *e=&g.ent[i];
		if ((e->state==EL_QUEUED || e->state==EL_OUT) && e->bl_vf==vf &&
		    e->bl_native==native && e->bl_crc==crc) return true;
	}
	return false;
}

static void mark_dual_locked(struct vframe_s *vf);

static bool pair_bl(struct vframe_s *vf)
{
	unsigned long flags;
	bool found = false;
	s64 pts;
	u32 crc;
	u64 native;

	if (!vf || !READ_ONCE(g.active) || src_fmt_off < 0)
		return false;
	if (!rpu_crc_of(vf, &crc)) {
		spin_lock_irqsave(&g.lock, flags);
		g.bl_no_crc++;
		spin_unlock_irqrestore(&g.lock, flags);
		return false;
	}
	native=get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off));
	spin_lock_irqsave(&g.lock, flags);
	if (paired_owned_locked(vf,native,crc)) {
		found=true;
		g.bl_reused++;
	} else if (g.bl_vf == vf && g.bl_native==native && g.bl_crc==crc) {
		found = false;
	} else if (!pts_of_crc(crc, &pts)) {
		g.bl_no_hint++;
		if (g.bl_no_hint <= 12 || !(g.bl_no_hint % 120)) {
			int match = -1, j;
			for (j = 0; j < g.nh; j++)
				if (g.hint[j].crc == crc) { match = j; break; }
			pr_info("dvfel: no hint crc=%08x pending=%d fullmatch=%d head=%08x elq=%d firstpts=%lld\n",
				crc, g.nh, match, g.nh ? g.hint[0].crc : 0,
				g.nq, g.nq ? g.q[0]->pts_us : -1LL);
		}
		g.bl_vf = vf;
		g.bl_found = false;
	} else {
		if (g.bl_paired + g.bl_no_el < 12 || !((g.bl_paired + g.bl_no_el) % 60))
			pr_info("dvfel: timestamps native=%llu hint=%lld crc=%08x elfirst=%lld ellast=%lld\n",
				get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off)), pts, crc,
				g.nq ? g.q[0]->pts_us : -1LL,
				g.nq ? g.q[g.nq-1]->pts_us : -1LL);
		while (g.nq && g.q[0]->pts_us < pts) {
			struct el_ent *e = q_remove(0);
			g.el_dropped++;
			dbg(2, "EL id %u pts %lld dropped (frame %lld)\n", e->id, e->pts_us, pts);
			ent_release_locked(e);
		}
		found = g.nq && g.q[0]->pts_us == pts;
		if (found) {
			g.q[0]->bl_vf=vf;
			g.q[0]->bl_native=native;
			g.q[0]->bl_crc=crc;
			g.bl_paired++;
			if (g.gap_pending) {
				g.gap_good_frames++;
				if (g.gap_good_frames == 2)
					g.gap_ready_after=jiffies+msecs_to_jiffies(34);
				else if (g.gap_good_frames > 2 && time_after_eq(jiffies,g.gap_ready_after))
					g.gap_pending=false;
			}
		}
		else
			g.bl_no_el++;
		g.bl_vf = vf;
		g.bl_found = found;
	}
	if (!found && blank_seek_gaps) {
		gap_arm_locked(false);
		g.gap_good_frames=0;
		g.gap_ready_after=g.gap_deadline;
	}
	g.bl_vf=vf; g.bl_native=native; g.bl_crc=crc; g.bl_found=found;
	if (found) {
		s7d_src_fmt(vf)->dual_layer = true;
		mark_dual_locked(vf);
	}
	spin_unlock_irqrestore(&g.lock, flags);
	return found;
}

/*
 * S7D uses amdv_wait_metadata_hw5 instead of amdv_wait_metadata.
 * The HW5 variant has a different signature and flow.
 */
static int wait_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* amdv_wait_metadata(vf, vd_path): VD1 only */
	*(struct vframe_s **)ri->data = regs->regs[1] == 0 ?
		(struct vframe_s *)regs->regs[0] : NULL;
	return 0;
}

static int wait_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct vframe_s *vf = *(struct vframe_s **)ri->data;
	unsigned long flags;
	u32 crc;
	s64 pts = 0;
	int i;
	bool have = false;

	if (!READ_ONCE(wait_el) || regs->regs[0] || !vf || !READ_ONCE(g.active) || !rpu_crc_of(vf, &crc))
		return 0;
	spin_lock_irqsave(&g.lock, flags);
	if (!g.active)
		goto out;
	if (g.bl_vf == vf && g.bl_native == get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off)) && g.bl_crc == crc) {
		if (g.bl_found)
			goto out;
		/* On S7D the wait itself calls is_dovi_dual_layer_frame, so this frame was
		 * already looked at, before its EL arrived. Forget that result: the frame
		 * waits below, and the next look (the retry or the parse) pairs it again. */
		g.bl_vf = NULL;
		g.wait_repairs++;
	}
	for (i = 0; i < min(g.nh, HINT_SEARCH); i++) {
		if (g.hint[i].crc == crc) {
			pts = g.hint[i].pts_us;
			have = true;
			break;
		}
	}
	if (!have)
		goto out;
	if (g.wait_vf != vf || g.wait_crc != crc || g.wait_native != get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off))) {
		unsigned int timeout=50;
		g.wait_vf = vf;
		g.wait_native=get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off));
		g.wait_crc = crc;
		if (seek_wait_ms && time_before(jiffies,g.seek_wait_until)) {
			timeout=clamp_t(unsigned int,seek_wait_ms,50,250);
			g.seek_wait_extended++;
		}
		g.wait_until = jiffies + msecs_to_jiffies(timeout);
		g.wait_expired = false;
	}
	while (g.nq && g.q[0]->pts_us < pts) {
		g.el_dropped++;
		ent_release_locked(q_remove(0));
	}
	if (g.nq && g.q[0]->pts_us >= pts) {
		if (g.q[0]->pts_us == pts)
			g.wait_ready++;
		goto out;
	}
	if (time_before(jiffies, g.wait_until)) {
		g.wait_checks++;
		regs->regs[0] = 1;
	} else if (!g.wait_expired) {
		g.wait_expired = true;
		g.wait_timeouts++;
		if (g.wait_timeouts <= 16) {
			int held=0,j;
			for(j=0;j<MAX_EL;j++) held+=g.ent[j].state==EL_OUT;
			pr_info("dvfel: EL wait expired pts=%lld queued=%d displayed=%d hints=%d\n",pts,g.nq,held,g.nh);
		}
	}
out:
	spin_unlock_irqrestore(&g.lock, flags);
	return 0;
}

/*
 * Frames pair_bl marked dual-layer. The decoder can reuse a vframe for the next play without
 * clearing src_fmt.dual_layer (seen after a killed session): the stock driver then treats that
 * play's single-layer frames as FEL and composes them with no EL (green garbage, one play).
 * Outside a session such a frame gets its flag cleared and is reported single-layer.
 */
#define MARKED_RING 64
static struct vframe_s *marked[MARKED_RING];
static unsigned int marked_head;
static unsigned long nosess_dual, nosess_cleared;

static void mark_dual_locked(struct vframe_s *vf)
{
	int i;
	for (i = 0; i < MARKED_RING; i++)
		if (marked[i] == vf)
			return;
	marked[marked_head++ % MARKED_RING] = vf;
}

static bool clear_stale_dual(struct vframe_s *vf)
{
	unsigned long flags;
	bool hit = false;
	int i;

	if (!vf || src_fmt_off < 0 || !s7d_src_fmt(vf)->dual_layer)
		return false;
	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MARKED_RING; i++)
		if (marked[i] == vf) { marked[i] = NULL; hit = true; break; }
	if (hit) {
		s7d_src_fmt(vf)->dual_layer = false;
		nosess_cleared++;
	}
	spin_unlock_irqrestore(&g.lock, flags);
	return hit;
}

/* ri->data: 1 = paired in this session (report dual), 2 = stale flag cleared (report single) */
static int dual_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct vframe_s *vf = (struct vframe_s *)regs->regs[0];
	int r = 0;

	if (pair_bl(vf))
		r = 1;
	else if (!READ_ONCE(g.active) && clear_stale_dual(vf))
		r = 2;
	*(int *)ri->data = r;
	return 0;
}

static int dual_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	int r = *(int *)ri->data;

	if (r == 1)
		regs->regs[0] = 1;
	else if (r == 2)
		regs->regs[0] = 0;
	else if (!READ_ONCE(g.active) && regs->regs[0])
		nosess_dual++;
	return 0;
}

static int parse_pre(struct kprobe *p, struct pt_regs *regs)
{
	pair_bl((struct vframe_s *)regs->regs[0]);
	return 0;
}

/* --- the "dveldec" provider --- */

static struct vframe_s *prov_peek(void *op_arg)
{
	struct vframe_s *vf = NULL;
	unsigned long flags;
	spin_lock_irqsave(&g.lock, flags);
	if (g.active && g.nq)
		vf = g.q[0]->vf;
	spin_unlock_irqrestore(&g.lock, flags);
	return vf;
}

static struct vframe_s *prov_get(void *op_arg)
{
	struct vframe_s *vf = NULL;
	unsigned long flags;
	spin_lock_irqsave(&g.lock, flags);
	if (g.active && g.nq) {
		struct el_ent *e = q_remove(0);
		e->state = EL_OUT;
		e->out_jiffies = jiffies;
		__module_get(THIS_MODULE);
		e->display_pin = true;
		e->sess = g.session_id;
		vf = e->vf;
		g.el_out++;
		dbg(2, "EL id %u crc %08x to display\n", e->id, e->crc);
	}
	spin_unlock_irqrestore(&g.lock, flags);
	return vf;
}

static void prov_put(struct vframe_s *vf, void *op_arg)
{
	unsigned long flags;
	int i;
	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MAX_EL; i++) {
		struct el_ent *e = &g.ent[i];
		if (e->state == EL_OUT && e->vf == vf) {
			g.el_put++;
			if (e->sess == g.session_id) g.sess_puts++;
			ent_release_locked(e);
			break;
		}
	}
	spin_unlock_irqrestore(&g.lock, flags);
	if (i == MAX_EL)
		dbg(1, "put of an unknown EL frame %p\n", vf);
	if (READ_ONCE(g.closing))
		schedule_delayed_work(&g.orphan_work,msecs_to_jiffies(100));
}

static int prov_event(int type, void *data, void *op_arg) { return 0; }

static int prov_states(struct vframe_states *states, void *op_arg)
{
	unsigned long flags;
	spin_lock_irqsave(&g.lock, flags);
	states->vf_pool_size = MAX_EL;
	states->buf_recycle_num = 0;
	states->buf_free_num = MAX_EL - g.nq;
	states->buf_avail_num = g.nq;
	spin_unlock_irqrestore(&g.lock, flags);
	return 0;
}

static const struct vframe_operations_s prov_ops = {
	.peek = prov_peek,
	.get = prov_get,
	.put = prov_put,
	.event_cb = prov_event,
	.vf_states = prov_states,
};

/* --- session management --- */

static void flush_queue(u32 what)
{
	unsigned long flags;
	spin_lock_irqsave(&g.lock, flags);
	if (what & DVFEL_SC2_FLUSH_EL) {
		if (g.active) g.seek_wait_until=jiffies+msecs_to_jiffies(1500);
		if (g.active && blank_seek_gaps) gap_arm_locked(true);
		else if (!g.active) g.gap_pending=false;
		while (g.nq) {
			g.el_flushed++;
			ent_release_locked(q_remove(0));
		}
		g.wait_vf = NULL;
		g.bl_vf = NULL;
		g.bl_found = false;
	}
	if (what & DVFEL_SC2_FLUSH_HINTS)
		g.nh = 0;
	spin_unlock_irqrestore(&g.lock, flags);
}

static long ioc_bl_hint(struct file *f, void __user *arg)
{
	struct dvfel_sc2_hint r;
	unsigned long flags;
	int i;

	if (copy_from_user(&r, arg, sizeof(r)))
		return -EFAULT;
	if (g.owner != f)
		return -EPERM;
	if (r.reserved)
		return -EINVAL;
	spin_lock_irqsave(&g.lock, flags);
	g.hints_in++;
	if (g.nh == MAX_HINTS) {
		memmove(&g.hint[0], &g.hint[1], (MAX_HINTS - 1) * sizeof(g.hint[0]));
		g.nh--;
		g.hints_overflow++;
	}
	for (i = g.nh; i > 0 && g.hint[i - 1].pts_us > r.pts_us; i--)
		;
	memmove(&g.hint[i + 1], &g.hint[i], (g.nh - i) * sizeof(g.hint[0]));
	g.hint[i].pts_us = r.pts_us;
	g.hint[i].crc = r.rpu_crc;
	g.nh++;
	spin_unlock_irqrestore(&g.lock, flags);
	return 0;
}

static const char *fence_driver_name(struct dma_fence *f) { return DRV_NAME; }
static const char *fence_timeline_name(struct dma_fence *f) { return "el"; }
static void el_fence_release(struct dma_fence *f) { kfree(f); module_put(THIS_MODULE); }

static const struct dma_fence_ops el_fence_ops = {
	.release = el_fence_release,
	.get_driver_name = fence_driver_name,
	.get_timeline_name = fence_timeline_name,
};

static void orphan_work_fn(struct work_struct *work)
{
	unsigned long flags;int i,n=0;
	mutex_lock(&g.sess_lock);
	if(!g.closing){mutex_unlock(&g.sess_lock);return;}
	spin_lock_irqsave(&g.lock,flags);
	/* Only this session's frames: leftovers of an earlier killed session would make every
	 * later drain wait the full DRAIN_TRIES; they are reclaimed by a later session. */
	for(i=0;i<MAX_EL;i++)n+=g.ent[i].state==EL_OUT && g.ent[i].sess==g.session_id;
	spin_unlock_irqrestore(&g.lock,flags);
	if(n && retire_stopped_frames()) n=0;
	/* Frames the display never returns (player killed, BL path gone) must not keep
	 * the provider registered: while dveldec is registered the stock driver runs every
	 * later play in its dual-layer path against the stale EL (green snow, even with no
	 * FEL session). Give up once the BL path has detached (it will not return them), or
	 * after DRAIN_TRIES. The frames stay tracked as EL_OUT; a later session reclaims them. */
	if(n){
		++g.drain_checks;
		if(!(READ_ONCE(g.bl_detached) && g.drain_checks>=3) && g.drain_checks<DRAIN_TRIES){
			schedule_delayed_work(&g.orphan_work,msecs_to_jiffies(100));
			mutex_unlock(&g.sess_lock);return;
		}
		g.drain_abandoned++;
		pr_warn("dvfel: drain gave up after %u checks (bl_detached=%d), %d EL frames never returned; detaching the provider\n",
			g.drain_checks,READ_ONCE(g.bl_detached),n);
	}
	if(g.prov_reg){vf_unreg_provider(&g.prov);g.prov_reg=false;}
	flush_work(&g.rel_work);
	WRITE_ONCE(g.closing,false);
	pr_info("dvfel: drain complete, provider detached (bl_detached=%d)\n",READ_ONCE(g.bl_detached));
	mutex_unlock(&g.sess_lock);
	module_put(THIS_MODULE);
}

/*
 * A player stopped or killed while its picture is on screen can leave its last EL
 * frames with the display for good: the BL path unregisters without returning them
 * (retire_stopped_frames is not ported to S7D). The drain then never completes and
 * every later session would be refused, so the next player silently loses FEL.
 * A new session adopts the still registered provider instead. The leftover frames
 * stay tracked as EL_OUT, so a late put from the display still releases them.
 */
static void adopt_draining_provider(void)
{
	unsigned long flags;
	int i, held = 0;

	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MAX_EL; i++)
		held += g.ent[i].state == EL_OUT;
	spin_unlock_irqrestore(&g.lock, flags);
	WRITE_ONCE(g.closing, false);
	WRITE_ONCE(g.bl_detached, false);
	g.adopts++;
	g.stale_adopted += held;
	fixes_start();
	pr_info("dvfel: new session adopts the draining provider, %d earlier EL frames still with the display\n", held);
}

/*
 * (g.lock held) Frees leftover display frames of earlier sessions when slots run low.
 * Only after this session's own frames have gone through the display and back, so the
 * display has replaced the old picture. The old frames' memory stays referenced until
 * here, so their vframe addresses cannot belong to a newer frame before this point.
 */
static void reclaim_stale_locked(void)
{
	int i, free = 0;

	for (i = 0; i < MAX_EL; i++)
		free += g.ent[i].state == EL_FREE;
	if (free >= MAX_EL / 2 || g.sess_puts < 50)
		return;
	for (i = 0; i < MAX_EL; i++) {
		struct el_ent *e = &g.ent[i];
		if (e->state == EL_OUT && e->sess != g.session_id) {
			g.stale_reclaimed++;
			ent_release_locked(e);
		}
	}
}

static long sess_start(struct file *f)
{
	long ret = 0;
	mutex_lock(&g.sess_lock);
	if (g.owner && g.owner != f) {
		ret = -EBUSY;
		goto out;
	}
	if (g.closing && !g.owner)
		adopt_draining_provider();
	if (!g.prov_reg) {
		if (vf_get_provider(EL_PROV_NAME)) {
			ret = -EBUSY;
			goto out;
		}
		WRITE_ONCE(g.bl_detached,false);
		__module_get(THIS_MODULE);
		fixes_start();
		vf_provider_init(&g.prov, EL_PROV_NAME, &prov_ops, NULL);
		ret = vf_reg_provider(&g.prov);
		if (ret < 0){ fixes_stop(); module_put(THIS_MODULE); goto out; }
		ret = 0;
		g.prov_reg = true;
	}
	if (!g.owner) {
		unsigned long flags;
		spin_lock_irqsave(&g.lock, flags);
		g.done_head = g.done_tail = 0;
		g.sess_puts = 0;
		spin_unlock_irqrestore(&g.lock, flags);
		WRITE_ONCE(g.session_id, g.session_id + 1);
	}
	WRITE_ONCE(g.owner, f);
	WRITE_ONCE(g.active, true);
	dbg(1, "session start\n");
out:
	mutex_unlock(&g.sess_lock);
	return ret;
}

static void sess_stop(struct file *f)
{
	mutex_lock(&g.sess_lock);
	if(g.owner!=f){mutex_unlock(&g.sess_lock);return;}
	WRITE_ONCE(g.active,false);
	WRITE_ONCE(g.closing,true);
	g.drain_checks=0;
	flush_queue(DVFEL_SC2_FLUSH_EL|DVFEL_SC2_FLUSH_HINTS);
	WRITE_ONCE(g.owner,NULL);
	wake_up_interruptible(&g.done_wq);
	fixes_stop();
	schedule_delayed_work(&g.orphan_work,msecs_to_jiffies(100));
	dbg(1,"session stop, waiting for display returns\n");
	mutex_unlock(&g.sess_lock);
}

static long ioc_el_queue(struct file *f, void __user *arg)
{
	struct dvfel_sc2_el r;
	struct el_ent *e = NULL;
	struct dma_buf *db;
	struct vframe_s *vf;
	struct dma_fence *fence;
	struct sync_file *sf = NULL;
	unsigned long flags;
	int i, fd;
	bool v4l_ref;

	if (copy_from_user(&r, arg, sizeof(r)))
		return -EFAULT;
	if (g.owner != f || !READ_ONCE(g.active))
		return -EPERM;
	if (r.reserved)
		return -EINVAL;
	db = dma_buf_get(r.fd);
	if (IS_ERR(db))
		return PTR_ERR(db);
	if (!dmabuf_is_uvm(db)) {
		dma_buf_put(db);
		return -ENODEV;
	}
	vf = el_vframe_get(db, &v4l_ref);
	if (IS_ERR_OR_NULL(vf)) { dma_buf_put(db); return vf ? PTR_ERR(vf) : -EINVAL; }
	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MAX_EL; i++)
		if (g.ent[i].state != EL_FREE && g.ent[i].db == db) break;
	spin_unlock_irqrestore(&g.lock, flags);
	if (i != MAX_EL) {
		el_vframe_put(db, v4l_ref); dma_buf_put(db); return -EBUSY;
	}
	/* Only vf->type is read through the headers: later vframe_s fields differ
	 * from this firmware, so the other values use offsets from the stock driver. */
	if (!g.el_in) pr_info("dvfel: native EL v4l=%d type=%x %ux%u depth=%x\n",
		v4l_ref, vf->type, *(u32 *)((u8 *)vf + 0x174), *(u32 *)((u8 *)vf + 0x178),
		*(u32 *)((u8 *)vf + 0x58));

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fence && fd >= 0) {
		__module_get(THIS_MODULE);
		dma_fence_init(fence, &el_fence_ops, &g.fence_lock, g.fence_ctx, ++g.fence_seq);
		sf = sync_file_create(fence);
	}
	if (!sf) {
		if (fd >= 0)
			put_unused_fd(fd);
		if (fence && fence->ops)
			dma_fence_put(fence);
		else
			kfree(fence);
		el_vframe_put(db, v4l_ref);
		dma_buf_put(db);
		return -ENOMEM;
	}

	spin_lock_irqsave(&g.lock, flags);
	reclaim_stale_locked();
	for (i = 0; i < MAX_EL; i++) {
		if (g.ent[i].state == EL_FREE) {
			e = &g.ent[i];
			break;
		}
	}
	if (e) {
		e->state = EL_QUEUED;
		e->bl_vf=NULL; e->bl_native=0; e->bl_crc=0;
		e->db = db;
		e->vf = vf;
		e->v4l_ref = v4l_ref;
		e->fence = fence;
		e->crc = r.rpu_crc;
		e->pts_us = r.pts_us;
		e->id = ++g.next_id;
		for (i = g.nq; i > 0 && g.q[i - 1]->pts_us > r.pts_us; i--)
			;
		memmove(&g.q[i + 1], &g.q[i], (g.nq - i) * sizeof(e));
		g.q[i] = e;
		g.nq++;
		g.el_in++;
		r.id = e->id;
		r.queued = g.nq;
	}
	spin_unlock_irqrestore(&g.lock, flags);
	if (!e) {
		fput(sf->file);
		put_unused_fd(fd);
		dma_fence_signal(fence);
		dma_fence_put(fence);
		el_vframe_put(db, v4l_ref);
		dma_buf_put(db);
		return -EBUSY;
	}
	r.fence_fd = fd;
	dbg(2, "EL id %u crc %08x pts %lld queued (%u)\n", r.id, r.rpu_crc, r.pts_us, r.queued);
	if (copy_to_user(arg, &r, sizeof(r))) {
		fput(sf->file);
		put_unused_fd(fd);
		return -EFAULT;
	}
	fd_install(fd, sf->file);
	return 0;
}

static bool done_ready(void)
{
	unsigned long flags;
	bool ready;
	spin_lock_irqsave(&g.lock, flags);
	ready = g.done_head != g.done_tail;
	spin_unlock_irqrestore(&g.lock, flags);
	return ready;
}

static long ioc_wait_done(struct file *f, void __user *arg)
{
	struct dvfel_sc2_done r;
	unsigned long flags;
	u64 session;
	long ret;

	if (copy_from_user(&r, arg, sizeof(r))) return -EFAULT;
	if (r.timeout_ms > DVFEL_SC2_MAX_WAIT_MS) return -EINVAL;
	if (mutex_lock_interruptible(&g.ioctl_lock)) return -ERESTARTSYS;
	if (g.owner != f || !g.active) {
		mutex_unlock(&g.ioctl_lock); return -EPERM;
	}
	session = g.session_id;
	mutex_unlock(&g.ioctl_lock);
	if (r.timeout_ms) {
		ret = wait_event_interruptible_timeout(g.done_wq,
			done_ready() || READ_ONCE(g.owner) != f || !READ_ONCE(g.active) ||
			READ_ONCE(g.session_id) != session, msecs_to_jiffies(r.timeout_ms));
		if (ret < 0) return ret;
	}
	if (mutex_lock_interruptible(&g.ioctl_lock)) return -ERESTARTSYS;
	if (g.owner != f || !g.active || g.session_id != session) {
		ret = -EPERM; goto out;
	}
	r.count = 0;
	memset(r.id, 0, sizeof(r.id));
	spin_lock_irqsave(&g.lock, flags);
	while (g.done_head != g.done_tail && r.count < DVFEL_SC2_MAX_DONE)
		r.id[r.count++] = g.done[g.done_tail++ % DONE_RING];
	spin_unlock_irqrestore(&g.lock, flags);
	ret = copy_to_user(arg, &r, sizeof(r)) ? -EFAULT : 0;
out:
	mutex_unlock(&g.ioctl_lock);
	return ret;
}

static long dvfel_ioctl_locked(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	switch (cmd) {
	case DVFEL_IOC_SC2_INFO: {
		struct dvfel_sc2_info r = {};
		strscpy(r.version, DVFEL_SC2_VERSION, sizeof(r.version));
		return copy_to_user(p, &r, sizeof(r)) ? -EFAULT : 0;
	}
	case DVFEL_IOC_SC2_START:
		return sess_start(f);
	case DVFEL_IOC_SC2_EL_QUEUE:
		return ioc_el_queue(f, p);
	case DVFEL_IOC_SC2_FLUSH: {
		u32 what;
		if (g.owner != f) return -EPERM;
		if (get_user(what, (u32 __user *)p)) return -EFAULT;
		if (what & ~(DVFEL_SC2_FLUSH_EL | DVFEL_SC2_FLUSH_HINTS)) return -EINVAL;
		flush_queue(what);
		return 0;
	}
	case DVFEL_IOC_SC2_STOP:
		if (g.owner != f) return -EPERM;
		sess_stop(f);
		return 0;
	case DVFEL_IOC_SC2_BL_HINT:
		return ioc_bl_hint(f, p);
	default:
		return -ENOTTY;
	}
}

static long dvfel_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long ret;
	if (cmd == DVFEL_IOC_SC2_WAIT_DONE)
		return ioc_wait_done(f, (void __user *)arg);
	if (mutex_lock_interruptible(&g.ioctl_lock)) return -ERESTARTSYS;
	ret = dvfel_ioctl_locked(f, cmd, arg);
	mutex_unlock(&g.ioctl_lock);
	return ret;
}

static int dvfel_release(struct inode *inode, struct file *f)
{
	mutex_lock(&g.ioctl_lock);
	sess_stop(f);
	mutex_unlock(&g.ioctl_lock);
	return 0;
}

static const struct file_operations dvfel_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dvfel_ioctl,
	.compat_ioctl = dvfel_ioctl,
	.release = dvfel_release,
};

static struct miscdevice dvfel_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRV_NAME,
	.fops = &dvfel_fops,
	.mode = 0666,	/* players are ordinary apps; session ownership is per open file */
};

static int stats_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	int i, out = 0;

	spin_lock_irqsave(&g.lock, flags);
	for (i = 0; i < MAX_EL; i++)
		out += g.ent[i].state == EL_OUT;
	seq_printf(m,"fixes session %d composer %d receiver %lu peek %lu canvas %lu register %lu\n",
		   fix_session,composer,fixed_receiver,fixed_peek,fixed_canvas,fixed_register);
	seq_printf(m,"draining %d checks %u abandoned %lu vd2_writes %lu vd2_changed %d\n",
		   g.closing,g.drain_checks,g.drain_abandoned,vd2_route_writes,vd2_changed);
	seq_printf(m,"restore pending %d vsync %lu skipped %lu libregs %lu now %d overflow %lu\n",
		   restore_pending,restore_vsync,restore_skipped,restore_libregs,lib_nregs,lib_overflow);
	seq_printf(m,"outside session: dual reported %lu, stale dual cleared %lu\n",nosess_dual,nosess_cleared);
	seq_printf(m,"bl_detached %d retired %llu\n",g.bl_detached,g.el_retired);
	seq_printf(m,"adopts %llu stale_adopted %llu stale_reclaimed %llu sess_puts %llu\n",
		   g.adopts,g.stale_adopted,g.stale_reclaimed,g.sess_puts);
	seq_printf(m,"seek_wait_ms %u extended %llu\n",seek_wait_ms,g.seek_wait_extended);
	seq_printf(m,"coherent_bl_fallback %d writes %lu\n",coherent_bl_fallback,fallback_mode_writes);
	seq_printf(m,"fallback_precommit %d writes %lu\n",fallback_precommit,fallback_precommit_writes);
	seq_printf(m,"tracked_mode %d skipped %lu native_writes %lu geometry_clears %lu\n",
		   tracked_composer_mode,fallback_mode_skips,fallback_native_mode_writes,fallback_geometry_clears);
	seq_printf(m,"blank_seek_gaps %d pending %d epochs %llu checks %llu\n",
		   blank_seek_gaps,g.gap_pending,g.gap_epochs,g.gap_blank_checks);
	seq_printf(m,"residual_unpaired %d hits %lu\n",residual_unpaired,residual_unpaired_hits);
	seq_printf(m,"residual_bypass %d hits %lu\n",residual_bypass,residual_checks_bypassed);
	seq_printf(m,"fel_composer_mode %d writes %lu changed %d\n",
		   fel_composer_mode,composer_mode_writes,composer_mode_changed);
	seq_printf(m, "version %s active %d provider %d probes %d/%d\n", DVFEL_SC2_VERSION,
		   g.active, g.prov_reg, krp_dual_ok, kp_parse_ok);
	seq_printf(m, "el in %llu out %llu put %llu dropped %llu flushed %llu queued %d on_display %d\n",
		   g.el_in, g.el_out, g.el_put, g.el_dropped, g.el_flushed, g.nq, out);
	seq_printf(m, "owned_pair_rechecks %llu\n",g.bl_reused);
	seq_printf(m, "wait checks %llu ready %llu timeouts %llu repairs %llu\n",
		   g.wait_checks, g.wait_ready, g.wait_timeouts, g.wait_repairs);
	seq_printf(m, "bl paired %llu no_el %llu no_hint %llu no_crc %llu\n",
		   g.bl_paired, g.bl_no_el, g.bl_no_hint, g.bl_no_crc);
	seq_printf(m, "hints in %llu pending %d skipped %llu overflow %llu\n",
		   g.hints_in, g.nh, g.hints_skipped, g.hints_overflow);
	spin_unlock_irqrestore(&g.lock, flags);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(stats);
DEFINE_SHOW_ATTRIBUTE(regs);

static int __init dvfel_init(void)
{
	int ret;

	if (!open_driver_fixed) {
		pr_err(DRV_NAME ": S7D open-driver integration not enabled; refusing to load\n");
		return -EOPNOTSUPP;
	}

	BUILD_BUG_ON(offsetof(struct vframe_src_fmt_s, sei_ptr) != 8);
	BUILD_BUG_ON(offsetof(struct vframe_src_fmt_s, sei_size) != 16);
	BUILD_BUG_ON(offsetof(struct vframe_src_fmt_s, dual_layer) != 20);

	src_fmt_off = measure_src_fmt_off();
	if (src_fmt_off < 0) {
		pr_err(DRV_NAME ": vframe layout of this firmware not found\n");
		return -EINVAL;
	}
	pr_info(DRV_NAME ": S7D vframe src_fmt measured at offset %d\n", src_fmt_off);

	spin_lock_init(&g.lock);
	spin_lock_init(&g.fence_lock);
	g.fence_ctx = dma_fence_context_alloc(1);
	mutex_init(&g.sess_lock);
	mutex_init(&g.ioctl_lock);
	init_waitqueue_head(&g.done_wq);
	INIT_WORK(&g.rel_work, rel_work_fn);
	INIT_DELAYED_WORK(&g.orphan_work, orphan_work_fn);

	ret=fixes_init(); if(ret)return ret;
	if (src_fmt_off != fw->src_fmt_off) {
		pr_err(DRV_NAME ": src_fmt at %d, profile expects %u\n", src_fmt_off, fw->src_fmt_off);
		fixes_exit();
		return -EINVAL;
	}

	krp_dual.kp.symbol_name = "is_dovi_dual_layer_frame";
	krp_dual.entry_handler = dual_entry;
	krp_dual.handler = dual_ret;
	krp_dual.data_size = sizeof(int);
	krp_dual.maxactive = 4;
	ret = register_kretprobe(&krp_dual);
	if (ret) {
		pr_err(DRV_NAME ": kretprobe is_dovi_dual_layer_frame: %d\n", ret);
		fixes_exit(); return ret;
	}
	krp_dual_ok = true;

	kp_parse.symbol_name = "amdv_parse_metadata";
	kp_parse.pre_handler = parse_pre;
	ret = register_kprobe(&kp_parse);
	if (ret) {
		pr_err(DRV_NAME ": kprobe amdv_parse_metadata: %d\n", ret);
		goto err_krp;
	}
	kp_parse_ok = true;

	/* The dispatcher amdv_wait_metadata(vf, vd_path), as on SC2: on S7D the video
	 * path did not reach amdv_wait_metadata_hw5 (no waits in play), and the
	 * dispatcher covers whichever variant this firmware uses. */
	krp_wait.kp.symbol_name = "amdv_wait_metadata";
	krp_wait.entry_handler = wait_entry;
	krp_wait.handler = wait_ret;
	krp_wait.data_size = sizeof(struct vframe_s *);
	krp_wait.maxactive = 8;
	ret = register_kretprobe(&krp_wait);
	if (ret)
		goto err_kp;

	ret = misc_register(&dvfel_misc);
	if (ret)
		goto err_wait;

	dbg_dir = debugfs_create_dir(DRV_NAME, NULL);
	debugfs_create_file("stats", 0444, dbg_dir, NULL, &stats_fops);
	debugfs_create_file("regs", 0444, dbg_dir, NULL, &regs_fops);
	debugfs_create_file("timing", 0644, dbg_dir, NULL, &timing_fops);

	pr_info(DRV_NAME ": S7D %s loaded (src_fmt %d)\n", DVFEL_SC2_VERSION, src_fmt_off);
	return 0;

err_wait:
	unregister_kretprobe(&krp_wait);
err_kp:
	unregister_kprobe(&kp_parse);
err_krp:
	unregister_kretprobe(&krp_dual);
	fixes_exit();
	return ret;
}

static void __exit dvfel_exit(void)
{
	int i;

	debugfs_remove_recursive(dbg_dir);
	timing_off();
	misc_deregister(&dvfel_misc);
	unregister_kretprobe(&krp_wait);
	unregister_kprobe(&kp_parse);
	unregister_kretprobe(&krp_dual);
	mutex_lock(&g.sess_lock);
	WRITE_ONCE(g.active, false);
	flush_queue(DVFEL_SC2_FLUSH_EL | DVFEL_SC2_FLUSH_HINTS);
	if (g.prov_reg) {
		vf_unreg_provider(&g.prov);
		g.prov_reg = false;
	}
	g.owner = NULL;
	mutex_unlock(&g.sess_lock);
	cancel_delayed_work_sync(&g.orphan_work);
	fixes_exit();
	for (i = 0; i < MAX_EL; i++)
		if (g.ent[i].state == EL_OUT)
			g.ent[i].state = EL_RELEASE;
	rel_work_fn(NULL);
	cancel_work_sync(&g.rel_work);
}

module_init(dvfel_init);
module_exit(dvfel_exit);
MODULE_DESCRIPTION("Dolby Vision FEL by the Dolby Vision core of Amlogic S7D (ONN 4K Pro v2)");
MODULE_AUTHOR("signde, kushmint");
MODULE_LICENSE("GPL");
MODULE_VERSION(DVFEL_SC2_VERSION);
