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
public:
	ContinuityFixer(PClip _child, int left, int top, int right, int bottom, int radius)
		: GenericVideoFilter(_child), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_radius(radius)
	{}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		int step = vi.ComponentSize();
		int width = frame->GetRowSize() / step;
		int height = frame->GetHeight();
		int stride = frame->GetPitch();

		size_t (*required_buffer)(int) = step == 2 ? edgefixer_required_buffer_w : edgefixer_required_buffer_b;
		void (*process_edge)(void *, const void *, int, int, int, int, void *) = step == 2 ? edgefixer_process_edge_w : edgefixer_process_edge_b;

		void *tmp = malloc(required_buffer(width > height ? width : height));
		if (!tmp)
			env->ThrowError("[ContinuityFixer] error allocating temporary buffer");

		BYTE *ptr = frame->GetWritePtr();

		// top
		for (int i = 0; i < m_top; ++i) {
			int ref_row = m_top - i;
			process_edge(ptr + stride * (ref_row - 1), ptr + stride * ref_row, step, step, width, m_radius, tmp);
		}

		// bottom
		for (int i = 0; i < m_bottom; ++i) {
			int ref_row = height - m_bottom - 1 + i;
			process_edge(ptr + stride * (ref_row + 1), ptr + stride * ref_row, step, step, width, m_radius, tmp);
		}

		// left
		for (int i = 0; i < m_left; ++i) {
			int ref_col = m_left - i;
			process_edge(ptr + step * (ref_col - 1), ptr + step * ref_col, stride, stride, height, m_radius, tmp);
		}

		// right
		for (int i = 0; i < m_right; ++i) {
			int ref_col = width - m_right - 1 + i;
			process_edge(ptr + step * (ref_col + 1), ptr + step * ref_col, stride, stride, height, m_radius, tmp);
		}

		free(tmp);

		return frame;
	}
};

class ReferenceFixer: public GenericVideoFilter {
	PClip m_reference;
	int m_left;
	int m_top;
	int m_right;
	int m_bottom;
	int m_radius;
public:
	ReferenceFixer(PClip _child, PClip reference, int left, int top, int right, int bottom, int radius)
		: GenericVideoFilter(_child), m_reference(reference), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_radius(radius)
	{}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		int step = vi.ComponentSize();
		int width = frame->GetRowSize() / step;
		int height = frame->GetHeight();
		int stride = frame->GetPitch();

		size_t (*required_buffer)(int) = step == 2 ? edgefixer_required_buffer_w : edgefixer_required_buffer_b;
		void (*process_edge)(void *, const void *, int, int, int, int, void *) = step == 2 ? edgefixer_process_edge_w : edgefixer_process_edge_b;

		void *tmp = malloc(required_buffer(width > height ? width : height));
		if (!tmp)
			env->ThrowError("[ReferenceFixer] error allocating temporary buffer");

		BYTE *write_ptr = frame->GetWritePtr();

		PVideoFrame ref_frame = m_reference->GetFrame(n, env);
		int ref_stride = ref_frame->GetPitch();

		const BYTE *read_ptr = ref_frame->GetReadPtr();

		// top
		for (int i = 0; i < m_top; ++i) {
			process_edge(write_ptr + stride * i, read_ptr + ref_stride * i, step, step, width, m_radius, tmp);
		}
		// bottom
		for (int i = 0; i < m_bottom; ++i) {
			process_edge(write_ptr + stride * (height - i - 1), read_ptr + ref_stride * (height - i - 1), step, step, width, m_radius, tmp);
		}
		// left
		for (int i = 0; i < m_left; ++i) {
			process_edge(write_ptr + step * i, read_ptr + step * i, stride, ref_stride, height, m_radius, tmp);
		}
		// right
		for (int i = 0; i < m_right; ++i) {
			process_edge(write_ptr + step * (width - i - 1), read_ptr + step * (width - i - 1), stride, ref_stride, height, m_radius, tmp);
		}

		free(tmp);

		return frame;
	}
};

AVSValue __cdecl Create_ContinuityFixer(AVSValue args, void *user_data, IScriptEnvironment *env)
{
	if (!args[0].AsClip()->GetVideoInfo().IsPlanar())
		env->ThrowError("[ContinuityFixer] input clip must be planar");
	if (args[0].AsClip()->GetVideoInfo().ComponentSize() > 2)
		env->ThrowError("[ContinuityFixer] input clip must be at most 16-bit");

	return new ContinuityFixer(args[0].AsClip(), args[1].AsInt(0), args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0));
}

AVSValue __cdecl Create_ReferenceFixer(AVSValue args, void *user_data, IScriptEnvironment *env)
{
	if (!args[0].AsClip()->GetVideoInfo().IsPlanar() || !args[1].AsClip()->GetVideoInfo().IsPlanar())
		env->ThrowError("[ReferenceFixer] clips must be planar");
	if (args[0].AsClip()->GetVideoInfo().width != args[1].AsClip()->GetVideoInfo().width || args[0].AsClip()->GetVideoInfo().height != args[1].AsClip()->GetVideoInfo().height)
		env->ThrowError("[ReferenceFixer] clips must have same dimensions");
	if (args[0].AsClip()->GetVideoInfo().ComponentSize() > 2 || args[1].AsClip()->GetVideoInfo().ComponentSize() > 2)
		env->ThrowError("[ReferenceFixer] clips must be at most 16-bit");
	if (args[0].AsClip()->GetVideoInfo().BitsPerComponent() != args[1].AsClip()->GetVideoInfo().BitsPerComponent())
		env->ThrowError("[ReferenceFixer] clips must have same bit depth");

	return new ReferenceFixer(args[0].AsClip(), args[1].AsClip(), args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0), args[6].AsInt(0));
}

extern "C" __declspec(dllexport)
const char * __stdcall AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *const vectors)
{
	AVS_linkage = vectors;

	env->AddFunction("ContinuityFixer", "c[left]i[top]i[right]i[bottom]i[radius]i", Create_ContinuityFixer, NULL);
	env->AddFunction("ReferenceFixer", "cc[left]i[top]i[right]i[bottom]i[radius]i", Create_ReferenceFixer, NULL);
	return "EdgeFixer";
}
