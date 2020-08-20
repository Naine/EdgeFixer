#include <stdlib.h>
#include <avisynth.h>

extern "C" {
#include "edgefixer.h"
}

const AVS_Linkage *AVS_linkage;

class ContinuityFixer: public GenericVideoFilter {
	int m_left;
	int m_top;
	int m_right;
	int m_bottom;
	int m_radius;
	int m_cleft;
	int m_ctop;
	int m_cright;
	int m_cbottom;
	int m_planes;
public:
	ContinuityFixer(PClip _child, int left, int top, int right, int bottom, int radius, int cleft, int ctop, int cright, int cbottom)
		: GenericVideoFilter(_child), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_radius(radius), m_cleft(cleft), m_ctop(ctop), m_cright(cright), m_cbottom(cbottom)
	{
		if (cleft | ctop | cright | cbottom)
		{
			m_planes = PLANAR_Y | PLANAR_U | PLANAR_V;
		}
		else if (vi.IsYUV() || vi.IsYUVA())
		{
			m_planes = PLANAR_Y;
		}
		else
		{
			m_planes = PLANAR_R | PLANAR_G | PLANAR_B;
		}
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		int step = vi.ComponentSize();
		size_t (*required_buffer)(int) = step == 2 ? edgefixer_required_buffer_w : edgefixer_required_buffer_b;

		void *tmp = malloc(required_buffer(vi.width > vi.height ? vi.width : vi.height));
		if (!tmp)
			env->ThrowError("[ContinuityFixer] error allocating temporary buffer");

		int planes_todo = m_planes;
		while (planes_todo)
		{
			int plane = planes_todo & -planes_todo; // extract lowest bit
			ProcessPlane(plane, frame, step, tmp);
			planes_todo &= ~plane;
		}

		free(tmp);

		return frame;
	}
private:
	void ProcessPlane(int plane, PVideoFrame& frame, int step, void *tmp)
	{
		int width = frame->GetRowSize(plane) / step;
		int height = frame->GetHeight(plane);
		int stride = frame->GetPitch(plane);

		void (*process_edge)(void *, const void *, int, int, int, int, void *) = step == 2 ? edgefixer_process_edge_w : edgefixer_process_edge_b;

		BYTE *ptr = frame->GetWritePtr(plane);

		int left, top, right, bottom;
		if (plane == PLANAR_U || plane == PLANAR_V)
		{
			left = m_cleft;
			top = m_ctop;
			right = m_cright;
			bottom = m_cbottom;
		}
		else
		{
			left = m_left;
			top = m_top;
			right = m_right;
			bottom = m_bottom;
		}

		// top
		for (int i = 0; i < top; ++i) {
			int ref_row = top - i;
			process_edge(ptr + stride * (ref_row - 1), ptr + stride * ref_row, step, step, width, m_radius, tmp);
		}

		// bottom
		for (int i = 0; i < bottom; ++i) {
			int ref_row = height - bottom - 1 + i;
			process_edge(ptr + stride * (ref_row + 1), ptr + stride * ref_row, step, step, width, m_radius, tmp);
		}

		// left
		for (int i = 0; i < left; ++i) {
			int ref_col = left - i;
			process_edge(ptr + step * (ref_col - 1), ptr + step * ref_col, stride, stride, height, m_radius, tmp);
		}

		// right
		for (int i = 0; i < right; ++i) {
			int ref_col = width - right - 1 + i;
			process_edge(ptr + step * (ref_col + 1), ptr + step * ref_col, stride, stride, height, m_radius, tmp);
		}
	}
};

class ReferenceFixer: public GenericVideoFilter {
	PClip m_reference;
	int m_left;
	int m_top;
	int m_right;
	int m_bottom;
	int m_radius;
	int m_cleft;
	int m_ctop;
	int m_cright;
	int m_cbottom;
	int m_planes;
public:
	ReferenceFixer(PClip _child, PClip reference, int left, int top, int right, int bottom, int radius, int cleft, int ctop, int cright, int cbottom)
		: GenericVideoFilter(_child), m_reference(reference), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_radius(radius), m_cleft(cleft), m_ctop(ctop), m_cright(cright), m_cbottom(cbottom)
	{
		if (cleft | ctop | cright | cbottom)
		{
			m_planes = PLANAR_Y | PLANAR_U | PLANAR_V;
		}
		else if (vi.IsYUV() || vi.IsYUVA())
		{
			m_planes = PLANAR_Y;
		}
		else
		{
			m_planes = PLANAR_R | PLANAR_G | PLANAR_B;
		}
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		int step = vi.ComponentSize();
		size_t (*required_buffer)(int) = step == 2 ? edgefixer_required_buffer_w : edgefixer_required_buffer_b;

		void *tmp = malloc(required_buffer(vi.width > vi.height ? vi.width : vi.height));
		if (!tmp)
			env->ThrowError("[ReferenceFixer] error allocating temporary buffer");

		PVideoFrame ref_frame = m_reference->GetFrame(n, env);
		int planes_todo = m_planes;
		while (planes_todo)
		{
			int plane = planes_todo & -planes_todo; // extract lowest bit
			ProcessPlane(plane, frame, ref_frame, step, tmp);
			planes_todo &= ~plane;
		}

		free(tmp);

		return frame;
	}
private:
	void ProcessPlane(int plane, PVideoFrame& frame, PVideoFrame& ref_frame, int step, void *tmp)
	{
		int width = frame->GetRowSize(plane) / step;
		int height = frame->GetHeight(plane);
		int stride = frame->GetPitch(plane);

		void (*process_edge)(void *, const void *, int, int, int, int, void *) = step == 2 ? edgefixer_process_edge_w : edgefixer_process_edge_b;

		BYTE *write_ptr = frame->GetWritePtr(plane);
		int ref_stride = ref_frame->GetPitch(plane);
		const BYTE *read_ptr = ref_frame->GetReadPtr(plane);

		int left, top, right, bottom;
		if (plane == PLANAR_U || plane == PLANAR_V)
		{
			left = m_cleft;
			top = m_ctop;
			right = m_cright;
			bottom = m_cbottom;
		}
		else
		{
			left = m_left;
			top = m_top;
			right = m_right;
			bottom = m_bottom;
		}

		// top
		for (int i = 0; i < top; ++i) {
			process_edge(write_ptr + stride * i, read_ptr + ref_stride * i, step, step, width, m_radius, tmp);
		}
		// bottom
		for (int i = 0; i < bottom; ++i) {
			process_edge(write_ptr + stride * (height - i - 1), read_ptr + ref_stride * (height - i - 1), step, step, width, m_radius, tmp);
		}
		// left
		for (int i = 0; i < left; ++i) {
			process_edge(write_ptr + step * i, read_ptr + step * i, stride, ref_stride, height, m_radius, tmp);
		}
		// right
		for (int i = 0; i < right; ++i) {
			process_edge(write_ptr + step * (width - i - 1), read_ptr + step * (width - i - 1), stride, ref_stride, height, m_radius, tmp);
		}
	}
};

AVSValue __cdecl Create_ContinuityFixer(AVSValue args, void *user_data, IScriptEnvironment *env)
{
	PClip clip = args[0].AsClip();
	const VideoInfo& vi = clip->GetVideoInfo();
	if (!vi.IsPlanar())
		env->ThrowError("[ContinuityFixer] input clip must be planar");
	if (vi.ComponentSize() > 2)
		env->ThrowError("[ContinuityFixer] input clip must be at most 16-bit");

	int cleft = args[6].AsInt(0);
	int ctop = args[7].AsInt(0);
	int cright = args[8].AsInt(0);
	int cbottom = args[9].AsInt(0);
	if (cleft | ctop | cright | cbottom)
	{
		if (vi.IsY() || !(vi.IsYUV() || vi.IsYUVA()))
			env->ThrowError("[ContinuityFixer] input clip must contain UV planes to process chroma");
	}

	return new ContinuityFixer(clip, args[1].AsInt(0), args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0), cleft, ctop, cright, cbottom);
}

AVSValue __cdecl Create_ReferenceFixer(AVSValue args, void *user_data, IScriptEnvironment *env)
{
	PClip clip1 = args[0].AsClip();
	PClip clip2 = args[1].AsClip();
	const VideoInfo& vi1 = clip1->GetVideoInfo();
	const VideoInfo& vi2 = clip2->GetVideoInfo();
	if (!vi1.IsPlanar() || !vi2.IsPlanar())
		env->ThrowError("[ReferenceFixer] clips must be planar");
	if (vi1.width != vi2.width || vi1.height != vi2.height)
		env->ThrowError("[ReferenceFixer] clips must have same dimensions");
	if (vi1.ComponentSize() > 2 || vi2.ComponentSize() > 2)
		env->ThrowError("[ReferenceFixer] clips must be at most 16-bit");
	if (vi1.BitsPerComponent() != vi2.BitsPerComponent())
		env->ThrowError("[ReferenceFixer] clips must have same bit depth");
	if (!!vi1.IsRGB() != !!vi2.IsRGB())
		env->ThrowError("[ReferenceFixer] clips must be both RGB or both YUV");

	int cleft = args[7].AsInt(0);
	int ctop = args[8].AsInt(0);
	int cright = args[9].AsInt(0);
	int cbottom = args[10].AsInt(0);
	if (cleft | ctop | cright | cbottom)
	{
		if (vi1.IsY() || vi2.IsY() || !(vi1.IsYUV() || vi1.IsYUVA()) || !(vi2.IsYUV() || vi2.IsYUVA()))
			env->ThrowError("[ReferenceFixer] clips must contain UV planes to process chroma");
		if (vi1.GetPlaneWidthSubsampling(PLANAR_U) != vi2.GetPlaneWidthSubsampling(PLANAR_U) || vi1.GetPlaneHeightSubsampling(PLANAR_U) != vi2.GetPlaneHeightSubsampling(PLANAR_U))
			env->ThrowError("[ReferenceFixer] clips must have same subsampling to process chroma");
	}

	return new ReferenceFixer(clip1, clip2, args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0), args[6].AsInt(0), cleft, ctop, cright, cbottom);
}

extern "C" __declspec(dllexport)
const char * __stdcall AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *const vectors)
{
	AVS_linkage = vectors;

	env->AddFunction("ContinuityFixer", "c[left]i[top]i[right]i[bottom]i[radius]i[cleft]i[ctop]i[cright]i[cbottom]i", Create_ContinuityFixer, NULL);
	env->AddFunction("ReferenceFixer", "cc[left]i[top]i[right]i[bottom]i[radius]i[cleft]i[ctop]i[cright]i[cbottom]i", Create_ReferenceFixer, NULL);
	return "EdgeFixer";
}
