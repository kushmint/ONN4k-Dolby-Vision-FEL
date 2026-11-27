/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Which Dolby Vision stream is playing, for the badge: profile and, for
 * profile 7, FEL or MEL. Works for every player (the RPU the decoder attaches
 * to each BL vframe), not only FEL sessions.
 *
 * debugfs dvfel/dvstream: "<seq> <profile> <el> <session>"
 *   seq      bumped for each new stream (first DV frame after 2 s without one,
 *            unless it continues the last stream's timeline: a pause)
 *   profile  4, 5, 7, 8 (0: not parsed)
 *   el       FEL, MEL or none
 *   session  1 if a dvfel FEL session was open when the stream was seen
 *            (libdvfelplay rewrites the RPU to profile 8.1 for the decoder,
 *            so the stream then reports what the session says: 7 FEL)
 *
 * The RPU syntax follows ETSI GS CCM 001 / quietvoid's dovi_tool (header,
 * mapping, NLQ); MEL = an NLQ that does nothing (dovi_tool is_mel).
 */

#define DVS_GAP_MS	2000
#define DVS_RESUME_US	3000000LL	/* a frame this close to the last one's pts continues it */
#define DVS_BUF		4096

struct dvs_bits {
	const u8 *p;
	u32 len, pos;	/* bits */
	bool err;
};

static u32 dvs_u(struct dvs_bits *b, unsigned int n)
{
	u32 v = 0;

	if (n > 32 || b->pos + n > b->len) {
		b->err = true;
		b->pos = b->len;
		return 0;
	}
	while (n--) {
		v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
		b->pos++;
	}
	return v;
}

static u32 dvs_ue(struct dvs_bits *b)
{
	unsigned int lz = 0;

	while (!b->err && !dvs_u(b, 1))
		if (++lz > 31) {
			b->err = true;
			return 0;
		}
	return lz ? ((1u << lz) - 1 + dvs_u(b, lz)) : 0;
}

static void dvs_se(struct dvs_bits *b)
{
	dvs_ue(b);
}

struct dvs_info {
	int profile;
	int el;		/* 0 none, 1 MEL, 2 FEL */
};

static bool dvs_parse(const u8 *rpu, u32 bytes, struct dvs_info *out)
{
	struct dvs_bits b = { rpu, bytes * 8, 0, false };
	u32 rpu_type, rpu_format, prof, coef_type = 0, denom = 0, bl_bits = 8, el_bits = 8, vdr_minus8 = 0;
	bool seq_info, el_resample = false, no_residual = true, use_prev, nlq = false;
	u32 nlq_method = 0, piv[3], cmp, i, j;
	bool mel = true;

	if (dvs_u(&b, 8) != 0x19)
		return false;
	rpu_type = dvs_u(&b, 6);
	rpu_format = dvs_u(&b, 11);
	if (rpu_type != 2)
		return false;
	prof = dvs_u(&b, 4);
	dvs_u(&b, 4);			/* vdr_rpu_level */
	seq_info = dvs_u(&b, 1);
	if (!seq_info)
		return false;		/* only the first RPU of a sequence is complete enough */
	{
		bool full_range;

		dvs_u(&b, 1);		/* chroma_resampling_explicit_filter_flag */
		coef_type = dvs_u(&b, 2);
		if (coef_type == 0)
			denom = dvs_ue(&b);
		dvs_u(&b, 2);		/* vdr_rpu_normalized_idc */
		full_range = dvs_u(&b, 1);
		if ((rpu_format & 0x700) == 0) {
			bl_bits = dvs_ue(&b) + 8;
			el_bits = (dvs_ue(&b) & 0xff) + 8;
			vdr_minus8 = dvs_ue(&b);
			dvs_u(&b, 1);	/* spatial_resampling_filter_flag */
			dvs_u(&b, 3);
			el_resample = dvs_u(&b, 1);
			no_residual = dvs_u(&b, 1);
		}
		if (b.err)
			return false;
		if (prof == 0)
			out->profile = full_range ? 5 : 4;
		else if (prof == 1)
			out->profile = (el_resample && !no_residual) ? (vdr_minus8 == 4 ? 7 : 4) : 8;
		else
			return false;
	}
	out->el = 0;
	if (out->profile != 7 && out->profile != 4)
		return true;
	/* profile 7 / 4: FEL or MEL from the NLQ data at the end of the mapping */
	if (coef_type != 0 || denom > 32 || bl_bits > 16 || el_bits > 16)
		return false;
	dvs_u(&b, 1);			/* vdr_dm_metadata_present_flag */
	use_prev = dvs_u(&b, 1);
	if (use_prev)
		return false;
	dvs_ue(&b);			/* vdr_rpu_id */
	dvs_ue(&b);			/* mapping_color_space */
	dvs_ue(&b);			/* mapping_chroma_format_idc */
	for (cmp = 0; cmp < 3; cmp++) {
		piv[cmp] = dvs_ue(&b) + 2;	/* num_pivots */
		if (piv[cmp] > 16)
			return false;
		for (i = 0; i < piv[cmp]; i++)
			dvs_u(&b, bl_bits);
	}
	if ((rpu_format & 0x700) == 0 && !no_residual) {
		nlq_method = dvs_u(&b, 3);
		nlq = true;
	}
	dvs_ue(&b);			/* num_x_partitions_minus1 */
	dvs_ue(&b);			/* num_y_partitions_minus1 */
	/* rpu_data_mapping */
	for (cmp = 0; cmp < 3 && !b.err; cmp++) {
		for (i = 0; i + 1 < piv[cmp] && !b.err; i++) {
			u32 idc = dvs_ue(&b);

			if (idc == 0) {
				u32 order = dvs_ue(&b);
				bool lin = order == 0 ? dvs_u(&b, 1) : false;

				if (order > 8)
					return false;
				if (lin) {
					dvs_ue(&b);
					dvs_u(&b, denom);
					if (i + 2 == piv[cmp]) {
						dvs_ue(&b);
						dvs_u(&b, denom);
					}
				} else {
					for (j = 0; j < order + 2; j++) {
						dvs_se(&b);
						dvs_u(&b, denom);
					}
				}
			} else if (idc == 1) {
				u32 order = dvs_u(&b, 2) + 1, k;

				dvs_se(&b);
				dvs_u(&b, denom);
				for (j = 0; j < order; j++)
					for (k = 0; k < 7; k++) {
						dvs_se(&b);
						dvs_u(&b, denom);
					}
			} else {
				return false;
			}
		}
	}
	if (!nlq || b.err)
		return false;
	/* rpu_data_nlq: one pivot, three components */
	for (cmp = 0; cmp < 3; cmp++) {
		u32 off = dvs_u(&b, el_bits);
		u32 in_max_int = dvs_ue(&b), in_max = dvs_u(&b, denom);

		if (off || in_max_int != 1 || in_max)
			mel = false;
		if (nlq_method == 0) {
			u32 si = dvs_ue(&b), s = dvs_u(&b, denom);
			u32 ti = dvs_ue(&b), t = dvs_u(&b, denom);

			if (si || s || ti || t)
				mel = false;
		}
	}
	if (b.err)
		return false;
	out->el = mel ? 1 : 2;
	return true;
}

static bool dvstream = true;
module_param(dvstream, bool, 0644);
MODULE_PARM_DESC(dvstream, "Recognise Dolby Vision streams for the badge (debugfs dvstream)");

static DEFINE_SPINLOCK(dvs_lock);
static u8 dvs_buf[DVS_BUF];
static struct vframe_s *dvs_last_vf;
static unsigned long dvs_last_jif;	/* last DV frame */
static s64 dvs_last_pts;
static bool dvs_pending;		/* new stream, not parsed yet */
static unsigned int dvs_tries;
static struct { u32 seq; int profile, el, session; } dvs_cur;
static unsigned long dvs_parsed, dvs_failed;

/* the RPU of a vframe without emulation prevention bytes, from its first 0x19 */
static u32 dvs_rpu_of(struct vframe_s *vf, u8 *dst, u32 cap)
{
	struct vframe_src_fmt_s *sf = s7d_src_fmt(vf);
	const u8 *p = sf->sei_ptr, *end;
	u32 size, type, i, n = 0, start;
	int zeros = 0;

	if (!p || sf->sei_size < 8 || sf->sei_size > 64 * 1024)
		return 0;
	end = p + sf->sei_size;
	while (end - p >= 8) {
		size = get_unaligned_be32(p);
		type = get_unaligned_be32(p + 4);
		p += 8;
		if (!size || size > end - p)
			return 0;
		if (type != AUX_DV_SEI) {
			p += size;
			continue;
		}
		/* skip a start code and the NAL header (0x7c 0x01) if present */
		for (start = 0; start < 8 && start < size && p[start] != 0x19; start++)
			;
		if (start == 8 || start >= size)
			return 0;
		for (i = start; i < size && n < cap; i++) {
			u8 c = p[i];

			if (zeros >= 2 && c == 3) {
				zeros = 0;
				continue;
			}
			zeros = c ? 0 : zeros + 1;
			dst[n++] = c;
		}
		return n;
	}
	return 0;
}

/* does the vframe carry an RPU (no copy) */
static bool dvs_has_rpu(struct vframe_s *vf)
{
	struct vframe_src_fmt_s *sf = s7d_src_fmt(vf);
	const u8 *p = sf->sei_ptr, *end;
	u32 size;

	if (!p || sf->sei_size < 8 || sf->sei_size > 64 * 1024)
		return false;
	end = p + sf->sei_size;
	while (end - p >= 8) {
		size = get_unaligned_be32(p);
		if (get_unaligned_be32(p + 4) == AUX_DV_SEI)
			return size > 0;
		p += 8;
		if (!size || size > end - p)
			return false;
		p += size;
	}
	return false;
}

/* every DV BL frame the driver looks at (parse and dual-layer probes) */
static void dvs_observe(struct vframe_s *vf)
{
	unsigned long flags, now = jiffies;
	struct dvs_info info = { 0, 0 };
	s64 pts;
	u32 n;

	if (!READ_ONCE(dvstream) || !vf || src_fmt_off < 0)
		return;
	/* the same frame is looked at several times per vsync: once is enough */
	if (vf == READ_ONCE(dvs_last_vf) && !READ_ONCE(dvs_pending))
		return;
	if (!spin_trylock_irqsave(&dvs_lock, flags))
		return;
	dvs_last_vf = vf;
	/* With the DV engine on (LLDV), HDR10 and SDR frames pass the parse probe too:
	 * only a frame carrying an RPU is Dolby Vision. */
	if (!dvs_has_rpu(vf))
		goto out;
	pts = (s64)get_unaligned((u64 *)((u8 *)vf + fw->vf_pts_off));
	if (!dvs_pending && time_after(now, dvs_last_jif + msecs_to_jiffies(DVS_GAP_MS)) &&
	    !(dvs_cur.seq && pts >= dvs_last_pts && pts - dvs_last_pts < DVS_RESUME_US)) {
		dvs_pending = true;
		dvs_tries = 0;
	}
	dvs_last_jif = now;
	dvs_last_pts = pts;
	if (dvs_pending) {
		n = dvs_rpu_of(vf, dvs_buf, DVS_BUF);
		if (n && dvs_parse(dvs_buf, n, &info)) {
			dvs_parsed++;
		} else {
			dvs_failed++;
			/* a stream without a complete RPU in its first frames: report the header guess or nothing */
			if (++dvs_tries < 48)
				goto out;
		}
		if (READ_ONCE(g.active)) {
			/* a FEL session: the decoder got the RPU rewritten to 8.1 */
			info.profile = 7;
			info.el = 2;
		}
		dvs_cur.profile = info.profile;
		dvs_cur.el = info.el;
		dvs_cur.session = READ_ONCE(g.active);
		dvs_cur.seq++;
		dvs_pending = false;
		pr_info("dvfel: DV stream %u: profile %d %s%s (pts %lld, tries %u)\n", dvs_cur.seq,
			info.profile, info.el == 2 ? "FEL" : info.el == 1 ? "MEL" : "no EL",
			dvs_cur.session ? ", FEL session" : "", pts, dvs_tries);
	}
out:
	spin_unlock_irqrestore(&dvs_lock, flags);
}

static int dvs_show(struct seq_file *m, void *v)
{
	unsigned long flags;

	spin_lock_irqsave(&dvs_lock, flags);
	seq_printf(m, "%u %d %s %d\n", dvs_cur.seq, dvs_cur.profile,
		   dvs_cur.el == 2 ? "FEL" : dvs_cur.el == 1 ? "MEL" : "none", dvs_cur.session);
	spin_unlock_irqrestore(&dvs_lock, flags);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dvs);
