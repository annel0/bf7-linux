/*
 * BF7: сборка потока H.264 для аппаратного декодера.
 *
 * VA-API отдаёт картинку уже разобранной, а наш декодер принимает целую
 * единицу доступа в Annex-B. Здесь и происходит обратный перевод:
 *
 *   - слайсы восстанавливать не нужно, их байты приходят как есть; достаточно
 *     приписать стартовый код и сложить подряд;
 *   - а вот SPS и PPS VA-API не передаёт вовсе, только разобранные поля.
 *     Поэтому заголовки пишутся заново, бит за битом, по полям
 *     VAPictureParameterBufferH264. Выдаём их только при изменении: лишняя
 *     смена набора параметров посреди потока декодеру ни к чему.
 *
 * Разбор заголовка слайса нужен ровно для одного: узнать, сколько опорных
 * кадров подразумевается по умолчанию, — в PPS это поле есть, а в параметрах
 * VA-API его нет.
 *
 * Основано на libva-v4l2 (https://github.com/radxa-pkg/libva-v4l2),
 * MIT License, Copyright (c) 2026 libva-v4l2 contributors. Оттуда взяты
 * порядок полей, таблицы зигзага и приём с перечитыванием заголовка слайса.
 * Обход для чужого железа (перезапись счётчиков порядка картинок) не
 * перенесён: наш декодер полноценный и принимает поток как есть.
 */
#include "bf7va.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bf7va {
namespace {

class Bits {
	std::vector<uint8_t> bytes_;
	unsigned position_ = 0;

public:
	void bit(unsigned value)
	{
		if (!(position_ & 7))
			bytes_.push_back(0);
		bytes_.back() |= (value & 1) << (7 - (position_++ & 7));
	}

	void bits(uint32_t value, unsigned count)
	{
		for (unsigned i = count; i; --i)
			bit(value >> (i - 1));
	}

	/* Экспоненциальный код Голомба без знака. */
	void ue(uint32_t value)
	{
		unsigned n = 0;
		uint32_t code = value + 1;

		for (uint32_t v = code; v >>= 1;)
			++n;
		for (unsigned i = 0; i < n; ++i)
			bit(0);
		bits(code, n + 1);
	}

	void se(int value)
	{
		ue(value <= 0 ? (uint32_t)(-2 * value) : (uint32_t)(2 * value - 1));
	}

	/*
	 * Завершить NAL и дописать его в поток: стартовый код, заголовок, а
	 * дальше байты с защитой от случайного появления стартового кода
	 * (после двух нулей подряд вставляется 0x03).
	 */
	void nal(std::vector<uint8_t> &out, uint8_t header)
	{
		unsigned zeros = 0;

		bit(1);
		while (position_ & 7)
			bit(0);
		out.insert(out.end(), { 0, 0, 0, 1, header });
		for (uint8_t v : bytes_) {
			if (zeros == 2 && v <= 3) {
				out.push_back(3);
				zeros = 0;
			}
			out.push_back(v);
			zeros = v == 0 ? zeros + 1 : 0;
		}
	}
};

/* Чтение заголовка слайса: та же защита от стартовых кодов, но наоборот. */
class Reader {
	std::vector<uint8_t> bytes_;
	size_t position_ = 0;
	bool ok_ = true;

public:
	explicit Reader(const std::vector<uint8_t> &bytes)
	{
		unsigned zeros = 0;

		for (size_t i = 1; i < bytes.size(); ++i) {
			uint8_t v = bytes[i];

			if (zeros == 2 && v == 3) {
				zeros = 0;
				continue;
			}
			bytes_.push_back(v);
			zeros = v == 0 ? zeros + 1 : 0;
		}
	}

	bool ok() const { return ok_; }

	unsigned bit()
	{
		if (position_ >= bytes_.size() * 8) {
			ok_ = false;
			return 0;
		}
		unsigned p = (unsigned)position_++;

		return (bytes_[p / 8] >> (7 - p % 8)) & 1;
	}

	uint32_t bits(unsigned count)
	{
		uint32_t v = 0;

		for (unsigned i = 0; i < count; ++i)
			v = (v << 1) | bit();
		return v;
	}

	uint32_t ue()
	{
		unsigned n = 0;

		while (ok_ && !bit())
			if (++n >= 31) {
				ok_ = false;
				return 0;
			}
		uint32_t v = 1;

		for (unsigned i = 0; i < n; ++i)
			v = (v << 1) | bit();
		return v - 1;
	}
};

const unsigned zigzag4[] = {
	0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15
};

const unsigned zigzag8[] = {
	 0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

/* Таблица квантования пишется разностями по порядку зигзага. */
void matrix(Bits &b, const uint8_t *values, const unsigned *scan, unsigned n)
{
	int last = 8;

	b.bit(1);
	for (unsigned j = 0; j < n; ++j) {
		int value = values[scan[j]];
		int delta = ((value - last + 128) & 255) - 128;

		b.se(delta);
		last = value;
	}
}

} /* namespace */

/*
 * Уровень потока. В параметрах VA-API его нет, а брать «с запасом» нельзя:
 * из уровня декодер выводит, сколько кадров ему разрешено копить перед
 * выдачей, и на 5.1 это шестнадцать кадров. Клиент VA-API ждёт первый кадр
 * прежде чем прислать второй, так что запас оборачивается взаимной
 * блокировкой. Берём наименьший уровень, в который помещается картинка
 * вместе со своими опорными кадрами.
 */
static unsigned level_for(unsigned mbs, unsigned refs)
{
	static const struct { unsigned idc, max_fs, max_dpb_mbs; } levels[] = {
		{ 10,	 99,	  396 }, { 11,	 396,	  900 },
		{ 12,	 396,	 2376 }, { 20,	 396,	 2376 },
		{ 21,	 792,	 4752 }, { 22,	1620,	 8100 },
		{ 30,	1620,	 8100 }, { 31,	3600,	18000 },
		{ 32,	5120,	20480 }, { 40,	8192,	32768 },
		{ 41,	8192,	32768 }, { 42,	8704,	34816 },
		{ 50, 22080,   110400 }, { 51, 36864,	184320 },
	};
	uint64_t need = (uint64_t)mbs * (refs + 1);

	for (const auto &l : levels)
		if (mbs <= l.max_fs && need <= l.max_dpb_mbs)
			return l.idc;
	return 52;
}

/* Почему поток собрать не вышло — иначе отказ выглядит немой ошибкой. */
static void h264_refuse(const char *why)
{
	if (getenv("BF7VA_TRACE"))
		fprintf(stderr, "bf7va: поток не собрать: %s\n", why);
}

H264Stream h264_access_unit(VAProfile profile, const Picture &pic,
			    H264State &state)
{
	const VAPictureParameterBufferH264 &p = pic.pic;
	const auto &s = p.seq_fields.bits;
	const auto &f = p.pic_fields.bits;
	H264Stream out;
	unsigned profile_idc = profile == VAProfileH264High ? 100 :
			       profile == VAProfileH264Main ? 77 : 66;

	/* Поддерживаем только 8 бит 4:2:0 — то, что умеет наше железо. */
	if (s.chroma_format_idc != 1 || p.bit_depth_luma_minus8 ||
	    p.bit_depth_chroma_minus8) {
		h264_refuse("не 8 бит 4:2:0");
		return {};
	}
	if (!s.frame_mbs_only_flag) {
		h264_refuse("чересстрочный поток");
		return {};
	}

	/* --- SPS --- */
	Bits sps;

	sps.bits(profile_idc, 8);
	sps.bits(profile_idc == 66 ? 0xc0 : 0, 8);
	sps.bits(level_for((p.picture_width_in_mbs_minus1 + 1) *
			   (p.picture_height_in_mbs_minus1 + 1),
			   p.num_ref_frames), 8);
	sps.ue(0);				/* seq_parameter_set_id */
	if (profile_idc >= 100) {
		sps.ue(1);			/* chroma_format_idc = 4:2:0 */
		sps.ue(0);			/* bit_depth_luma_minus8 */
		sps.ue(0);			/* bit_depth_chroma_minus8 */
		sps.bit(0);			/* qpprime_y_zero_transform_bypass */
		sps.bit(0);			/* seq_scaling_matrix_present */
	}
	sps.ue(s.log2_max_frame_num_minus4);
	sps.ue(s.pic_order_cnt_type);
	if (!s.pic_order_cnt_type)
		sps.ue(s.log2_max_pic_order_cnt_lsb_minus4);
	else if (s.pic_order_cnt_type == 1) {
		h264_refuse("счёт порядка показа вида 1");
		return {};
	}
	sps.ue(p.num_ref_frames);
	sps.bit(s.gaps_in_frame_num_value_allowed_flag);
	sps.ue(p.picture_width_in_mbs_minus1);
	/* У VA-API высота в макроблоках кадра, в SPS — в единицах карты. */
	sps.ue((p.picture_height_in_mbs_minus1 + 1) /
	       (2 - s.frame_mbs_only_flag) - 1);
	sps.bit(s.frame_mbs_only_flag);
	if (!s.frame_mbs_only_flag)
		sps.bit(s.mb_adaptive_frame_field_flag);
	sps.bit(s.direct_8x8_inference_flag);
	sps.bit(0);	/* обрезкой кадра распоряжается сам VA-API */

	/*
	 * VUI. Из всего набора нам нужна одна вещь — ограничения потока, где
	 * сказано, что переставлять кадры не нужно. Порядок показа VA-API
	 * ведёт сам: он просит декодировать в конкретную поверхность и сам
	 * решает, когда её показать. Поэтому декодеру велим отдавать кадры в
	 * порядке декодирования, сразу, ничего не накапливая.
	 */
	sps.bit(1);				/* vui_parameters_present */
	sps.bit(0);				/* aspect_ratio_info_present */
	sps.bit(0);				/* overscan_info_present */
	sps.bit(0);				/* video_signal_type_present */
	sps.bit(0);				/* chroma_loc_info_present */
	sps.bit(0);				/* timing_info_present */
	sps.bit(0);				/* nal_hrd_parameters_present */
	sps.bit(0);				/* vcl_hrd_parameters_present */
	sps.bit(0);				/* pic_struct_present */
	sps.bit(1);				/* bitstream_restriction */
	sps.bit(1);				/* motion_vectors_over_pic_boundaries */
	sps.ue(0);				/* max_bytes_per_pic_denom */
	sps.ue(0);				/* max_bits_per_mb_denom */
	sps.ue(16);				/* log2_max_mv_length_horizontal */
	sps.ue(16);				/* log2_max_mv_length_vertical */
	sps.ue(0);				/* max_num_reorder_frames */
	sps.ue(p.num_ref_frames);		/* max_dec_frame_buffering */

	std::vector<uint8_t> new_sps;

	sps.nal(new_sps, 0x67);
	if (new_sps != state.sps) {
		out.config.insert(out.config.end(), new_sps.begin(), new_sps.end());
		state.sps = new_sps;
	}

	/*
	 * --- PPS ---
	 *
	 * Число опорных кадров по умолчанию в параметрах VA-API отсутствует, а
	 * в PPS оно обязательно. Достаём его из заголовков самих слайсов: там,
	 * где слайс не переопределяет значение явно, оно и есть умолчание.
	 */
	std::map<unsigned, std::pair<int, int>> defaults;

	for (const auto &sl : pic.slices) {
		const std::vector<uint8_t> &bytes = sl.second;

		if (bytes.empty())
			continue;
		unsigned nal_type = bytes[0] & 31;

		if (nal_type != 1 && nal_type != 5)
			continue;

		Reader r(bytes);

		r.ue();				/* first_mb_in_slice */
		unsigned raw_type = r.ue();
		unsigned id = r.ue();		/* pic_parameter_set_id */

		if (!r.ok() || raw_type > 9 || id > 255)
			continue;

		unsigned type = raw_type % 5;

		if (type > 2)
			continue;		/* SP/SI не поддерживаем */

		auto it = defaults.emplace(id, std::make_pair(-1, -1)).first;

		if (type == 2)
			continue;		/* у I-слайсов опорных нет */

		r.bits(s.log2_max_frame_num_minus4 + 4);
		bool field = false;

		if (!s.frame_mbs_only_flag) {
			field = r.bit();
			if (field)
				r.bit();
		}
		if (nal_type == 5)
			r.ue();			/* idr_pic_id */
		if (!s.pic_order_cnt_type) {
			r.bits(s.log2_max_pic_order_cnt_lsb_minus4 + 4);
			if (f.pic_order_present_flag && !field)
				r.ue();
		}
		if (f.redundant_pic_cnt_present_flag)
			r.ue();
		if (type == 1)
			r.bit();		/* direct_spatial_mv_pred_flag */
		if (!r.ok())
			continue;
		if (r.bit())
			continue;		/* слайс задаёт своё — не умолчание */

		auto merge = [](int &prev, unsigned count) {
			if (count <= 31 && (prev < 0 || (unsigned)prev == count))
				prev = (int)count;
		};

		merge(it->second.first, sl.first.num_ref_idx_l0_active_minus1);
		if (type == 1)
			merge(it->second.second,
			      sl.first.num_ref_idx_l1_active_minus1);
	}

	for (const auto &kv : defaults) {
		Bits pps;

		pps.ue(kv.first);		/* pic_parameter_set_id */
		pps.ue(0);			/* seq_parameter_set_id */
		pps.bit(f.entropy_coding_mode_flag);
		pps.bit(f.pic_order_present_flag);
		pps.ue(0);			/* num_slice_groups_minus1 */
		pps.ue((uint32_t)std::max(0, kv.second.first));
		pps.ue((uint32_t)std::max(0, kv.second.second));
		pps.bit(f.weighted_pred_flag);
		pps.bits(f.weighted_bipred_idc, 2);
		pps.se(p.pic_init_qp_minus26);
		pps.se(p.pic_init_qs_minus26);
		pps.se(p.chroma_qp_index_offset);
		pps.bit(f.deblocking_filter_control_present_flag);
		pps.bit(f.constrained_intra_pred_flag);
		pps.bit(f.redundant_pic_cnt_present_flag);
		if (profile_idc >= 100) {
			pps.bit(f.transform_8x8_mode_flag);
			pps.bit(pic.have_iq);
			if (pic.have_iq) {
				for (unsigned j = 0; j < 6; ++j)
					matrix(pps, pic.iq.ScalingList4x4[j],
					       zigzag4, 16);
				if (f.transform_8x8_mode_flag)
					for (unsigned j = 0; j < 2; ++j)
						matrix(pps, pic.iq.ScalingList8x8[j],
						       zigzag8, 64);
			}
			pps.se(p.second_chroma_qp_index_offset);
		}

		std::vector<uint8_t> new_pps;

		pps.nal(new_pps, 0x68);
		if (new_pps != state.pps[kv.first]) {
			out.config.insert(out.config.end(), new_pps.begin(),
					  new_pps.end());
			state.pps[kv.first] = new_pps;
		}
	}

	/* --- слайсы: просто стартовый код и байты как есть --- */
	for (const auto &sl : pic.slices) {
		if (sl.second.empty())
			continue;
		out.picture.insert(out.picture.end(), { 0, 0, 0, 1 });
		out.picture.insert(out.picture.end(), sl.second.begin(),
				   sl.second.end());
	}
	return out;
}

} /* namespace bf7va */
