// Time of day, drawn over the finished picture. Civ draws the whole world (land, water, units, clouds) straight onto
// the back buffer from command lists recorded on its worker contexts, then its interface. Any context may record
// either: so each list is classed as it is recorded, by its back-buffer binds (the world draws with a depth buffer,
// the interface without one). On the immediate context, just before the first interface list after the frame's world
// (no world list still waiting), the picture is copied aside and drawn back through the grade (CgLooks), so the world
// takes the hour's colours and the interface keeps its own: one copy and one full-screen draw a frame. A frame whose
// world ran with no interface list after it is graded at Present instead (interface included), counted as late.
//
// Hooks (through a throwaway device's vtables, which the game's share): Present, ExecuteCommandList on the immediate
// context, and FinishCommandList and the render-target binds on deferred ones. Direct3D 11 is reached through raw vtable
// slots (no headers in this build); the shader is compiled at run time with d3dcompiler_47. Logged to Logs\ColorGrade.log.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "CgLooks.h"
#include "ColorGrade.h"

namespace
{
	// ---- log ----
	int s_lines = 0;
	void Note(const char* format, ...)
	{
		if(s_lines > 20000) return;
		++s_lines;
		char path[MAX_PATH];
		DWORD n = GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH);
		if(n == 0 || n >= MAX_PATH) return;
		strcat_s(path, "\\Documents\\My Games\\Sid Meier's Civilization 5\\Logs\\ColorGrade.log");
		FILE* f = NULL;
		if(fopen_s(&f, path, "a") != 0 || !f) return;
		va_list args;
		va_start(args, format);
		vfprintf(f, format, args);
		va_end(args);
		fputc('\n', f);
		fclose(f);
	}

	// ---- raw COM ----
	typedef void** Vt;
	inline Vt VT(void* p) { return *(Vt*)p; }
	template<class F> F Slot(void* p, int i) { return (F)VT(p)[i]; }
	void Release(void* p) { if(p) Slot<ULONG(__stdcall*)(void*)>(p, 2)(p); }

	const GUID IID_Device = { 0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };
	const GUID IID_Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

	struct Tex2DDesc { UINT Width, Height, MipLevels, ArraySize; DXGI_FORMAT Format; DXGI_SAMPLE_DESC SampleDesc; UINT Usage, BindFlags, CPUAccessFlags, MiscFlags; };
	struct BufferDesc { UINT ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride; };
	struct Viewport { float X, Y, W, H, MinZ, MaxZ; };
	struct ViewDesc { UINT Format, Dimension, a, b, c, d; };   // shader resource / render target view description

	enum
	{
		PSSetShaderResources = 8, PSSetShader = 9, VSSetShader = 11, Draw = 13, PSSetConstantBuffers = 16,
		IASetInputLayout = 17, GSSetShader = 23, IASetPrimitiveTopology = 24, OMSetRenderTargets = 33, OMSetRenderTargetsAndUAV = 34,
		OMSetBlendState = 35, OMSetDepthStencilState = 36, RSSetState = 43, RSSetViewports = 44,
		CopyResource = 47, UpdateSubresource = 48, ExecuteCommandList = 58, HSSetShader = 60, DSSetShader = 64,
		PSGetShaderResources = 73, PSGetShader = 74, VSGetShader = 76, PSGetConstantBuffers = 77, IAGetInputLayout = 78,
		GSGetShader = 82, IAGetPrimitiveTopology = 83, OMGetRenderTargets = 89, OMGetBlendState = 91,
		OMGetDepthStencilState = 92, RSGetState = 94, RSGetViewports = 95, HSGetShader = 98, DSGetShader = 102,
		FinishCommandList = 114
	};
	enum { CreateBuffer = 3, CreateTexture2D = 5, CreateSRV = 7, CreateRTV = 9, CreateVS = 12, CreatePS = 15, CreateDeferred = 27, GetImmediate = 40 };
	enum { ViewGetResource = 7, SwapGetDevice = 7, SwapPresent = 8, SwapGetBuffer = 9, ResourceGetType = 7, TextureGetDesc = 10 };

	typedef HRESULT(__stdcall* CreateDeviceAndSwapChainFn)(void*, int, HMODULE, UINT, const void*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*,
		void**, void**, int*, void**);
	typedef HRESULT(__stdcall* CompileFn)(const void*, SIZE_T, const char*, const void*, void*, const char*, const char*, UINT, UINT, void**, void**);
	typedef HRESULT(__stdcall* PresentFn)(void*, UINT, UINT);
	typedef void(__stdcall* ExecFn)(void*, void*, BOOL);
	typedef HRESULT(__stdcall* FinishFn)(void*, BOOL, void**);

	// ---- settings, written from the game's Lua and read each frame ----
	volatile int s_mode = 0;              // 0 off, otherwise the day sits at s_target (the panel's hour)
	volatile float s_target = 0;
	volatile float s_strength = 1;
	volatile float s_nightDepth = 1;
	volatile float s_brightness = 1, s_colour = 1, s_contrast = 1;
	volatile float s_warmth = 1;
	volatile float s_phase = 0.1f;

	// ---- hooks ----
	PresentFn s_present = NULL;
	ExecFn s_exec = NULL;
	FinishFn s_finish = NULL;
	Vt s_swapVt = NULL, s_execVt = NULL, s_finishVt = NULL, s_omVt = NULL;

	// While the plugin draws on a thread, its own hooks on that thread step aside: the graphics layer's functions can
	// call one another through the same (hooked) table.
	DWORD s_tls = TLS_OUT_OF_INDEXES;
	inline bool Inside() { return s_tls != TLS_OUT_OF_INDEXES && TlsGetValue(s_tls) != NULL; }
	struct Guard
	{
		bool was;
		Guard() { was = Inside(); if(s_tls != TLS_OUT_OF_INDEXES) TlsSetValue(s_tls, (void*)1); }
		~Guard() { if(s_tls != TLS_OUT_OF_INDEXES && !was) TlsSetValue(s_tls, NULL); }
	};

	// ---- the grade ----
	void* volatile s_device = NULL;
	void* s_ctx = NULL;      // the immediate context
	void* s_vs = NULL;
	void* s_ps = NULL;
	void* s_cb = NULL;
	bool s_broken = false;
	float s_params[11];      // this frame's grade: mul rgb, sat, sheen rgb, k, gamma, strength, contrast

	const char* const kShader =
		"Texture2D src : register(t0);\n"
		"cbuffer Grade : register(b0) { float4 mulSat; float4 sheenK; float4 gammaStrength; };\n"
		"float4 vs(uint id : SV_VertexID) : SV_Position {\n"
		"  float2 uv = float2((id << 1) & 2, id & 2);\n"
		"  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
		"}\n"
		"float3 grade(float3 c) {\n"
		"  float l = dot(c, float3(0.299, 0.587, 0.114));\n"
		"  float3 a = l + (c - l) * mulSat.w;\n"
		"  a = a * mulSat.rgb + sheenK.w * l * l * sheenK.rgb;\n"
		"  a = (a - 0.5) * (1 + gammaStrength.w) + 0.5;\n"
		"  a = pow(saturate(a), gammaStrength.x);\n"
		"  return lerp(c, a, gammaStrength.y);\n"
		"}\n"
		"float4 ps(float4 p : SV_Position) : SV_Target {\n"
		"  float4 c = src.Load(int3(p.xy, 0));\n"
		"  return float4(grade(c.rgb), c.a);\n"
		"}\n";

	// ---- the picture: the back buffer, and our copy of it to read from ----
	void* s_back = NULL;     // the swap chain's back buffer (not held)
	void* s_copy = NULL;
	void* s_copyView = NULL;
	Tex2DDesc s_backDesc;
	inline UINT ViewFormat(UINT f) { return f == 27 ? 28 : f == 90 ? 87 : f; }   // typeless to plain RGBA / BGRA

	// ---- what each command list draws: the world binds the back buffer with a depth buffer, the interface without one ----
	// Which context records a list says nothing: Civ hands world work to any of its contexts, the one that records the
	// interface included (a frame can be all of its lists). So each deferred context's render-target binds are watched
	// while it records, and each list is kept with its kind: world, interface, or neither (off-screen work).
	enum { kOther = 0, kWorld = 1, kFace = 2 };
	struct ListOwner { void* list; void* ctx; int kind; LONG frame; bool ran; };
	ListOwner s_owners[4096];   // a busy frame records hundreds of lists; too few and its first lists go unknown
	LONG s_ownerNext = 0;
	SRWLOCK s_ownerLock = SRWLOCK_INIT;
	struct CtxMarks { void* ctx; LONG depth, plain; };   // back-buffer binds with and without depth since its last list
	CtxMarks s_marks[64];
	LONG s_markN = 0;
	SRWLOCK s_markLock = SRWLOCK_INIT;
	CtxMarks* MarksOf(void* ctx)
	{
		LONG n = s_markN;
		for(LONG i = 0; i < n; ++i) if(s_marks[i].ctx == ctx) return &s_marks[i];
		AcquireSRWLockExclusive(&s_markLock);
		CtxMarks* m = NULL;
		for(LONG i = 0; i < s_markN && !m; ++i) if(s_marks[i].ctx == ctx) m = &s_marks[i];
		if(!m && s_markN < 64) { m = &s_marks[s_markN]; m->ctx = ctx; m->depth = m->plain = 0; MemoryBarrier(); ++s_markN; }
		ReleaseSRWLockExclusive(&s_markLock);
		return m;
	}

	volatile LONG s_frame = 0;          // Presents so far
	volatile LONG s_gradedFrame = -1;   // the frame last graded
	volatile LONG s_graded = 0, s_late = 0;
	bool s_sawFace = false;             // an interface list has been seen
	// Safety: a frame shaded at a different point from its neighbours flashes (CgSafety: a lone blip is logged, a shade
	// that keeps flashing stops for the session).
	CgSafety s_safety;
	bool s_worldRan = false;            // a world list ran this frame
	int s_worldAfter = 0;               // world lists run after the grade this frame
	LONG s_waited = 0, s_after = 0, s_noWorld = 0;   // frames: interface held back for pending world, world after the grade, no world
	// The frame's lists in the order run: w world, i interface, o neither; '|' where the grade went in.
	char s_order[128], s_lastOrder[128] = "";
	int s_orderLen = 0;
	int s_orderNotes = 0;
	void OrderAdd(char c) { if(s_orderLen < (int)sizeof(s_order) - 1) s_order[s_orderLen++] = c; }

	// The immediate context's state, kept while the grade draws and put back after.
	struct Saved
	{
		void* layout; int topology; void* vs; void* ps; void* gs; void* hs; void* ds; void* cb;
		void* blend; float factor[4]; UINT mask; void* depth; UINT ref; void* raster; UINT nvp; Viewport vp[16];
		void* rtv[8]; void* dsv;
		void* srvs[128];   // every pixel-shader texture slot: binding the back buffer as a target unbinds it from all of them
	};

	void Save(void* ctx, Saved& s)
	{
		UINT zero = 0;
		Slot<void(__stdcall*)(void*, void**)>(ctx, IAGetInputLayout)(ctx, &s.layout);
		Slot<void(__stdcall*)(void*, int*)>(ctx, IAGetPrimitiveTopology)(ctx, &s.topology);
		zero = 0; Slot<void(__stdcall*)(void*, void**, void**, UINT*)>(ctx, VSGetShader)(ctx, &s.vs, NULL, &zero);
		zero = 0; Slot<void(__stdcall*)(void*, void**, void**, UINT*)>(ctx, PSGetShader)(ctx, &s.ps, NULL, &zero);
		zero = 0; Slot<void(__stdcall*)(void*, void**, void**, UINT*)>(ctx, GSGetShader)(ctx, &s.gs, NULL, &zero);
		zero = 0; Slot<void(__stdcall*)(void*, void**, void**, UINT*)>(ctx, HSGetShader)(ctx, &s.hs, NULL, &zero);
		zero = 0; Slot<void(__stdcall*)(void*, void**, void**, UINT*)>(ctx, DSGetShader)(ctx, &s.ds, NULL, &zero);
		Slot<void(__stdcall*)(void*, UINT, UINT, void**)>(ctx, PSGetShaderResources)(ctx, 0, 128, s.srvs);
		Slot<void(__stdcall*)(void*, UINT, UINT, void**)>(ctx, PSGetConstantBuffers)(ctx, 0, 1, &s.cb);
		Slot<void(__stdcall*)(void*, void**, float*, UINT*)>(ctx, OMGetBlendState)(ctx, &s.blend, s.factor, &s.mask);
		Slot<void(__stdcall*)(void*, void**, UINT*)>(ctx, OMGetDepthStencilState)(ctx, &s.depth, &s.ref);
		Slot<void(__stdcall*)(void*, void**)>(ctx, RSGetState)(ctx, &s.raster);
		s.nvp = 16;
		Slot<void(__stdcall*)(void*, UINT*, Viewport*)>(ctx, RSGetViewports)(ctx, &s.nvp, s.vp);
		Slot<void(__stdcall*)(void*, UINT, void**, void**)>(ctx, OMGetRenderTargets)(ctx, 8, s.rtv, &s.dsv);
	}

	// Puts back what Save took. Civ's targets go back before its textures: a texture rebound while it is still our target
	// would be refused (bound as nothing).
	void Restore(void* ctx, Saved& s)
	{
		Slot<void(__stdcall*)(void*, void*)>(ctx, IASetInputLayout)(ctx, s.layout);
		Slot<void(__stdcall*)(void*, int)>(ctx, IASetPrimitiveTopology)(ctx, s.topology);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, VSSetShader)(ctx, s.vs, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, PSSetShader)(ctx, s.ps, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, GSSetShader)(ctx, s.gs, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, HSSetShader)(ctx, s.hs, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, DSSetShader)(ctx, s.ds, NULL, 0);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetConstantBuffers)(ctx, 0, 1, &s.cb);
		Slot<void(__stdcall*)(void*, void*, const float*, UINT)>(ctx, OMSetBlendState)(ctx, s.blend, s.factor, s.mask);
		Slot<void(__stdcall*)(void*, void*, UINT)>(ctx, OMSetDepthStencilState)(ctx, s.depth, s.ref);
		Slot<void(__stdcall*)(void*, void*)>(ctx, RSSetState)(ctx, s.raster);
		if(s.nvp > 0) Slot<void(__stdcall*)(void*, UINT, const Viewport*)>(ctx, RSSetViewports)(ctx, s.nvp, s.vp);
		UINT nrt = 0;
		for(UINT i = 0; i < 8; ++i) if(s.rtv[i]) nrt = i + 1;
		Slot<void(__stdcall*)(void*, UINT, void* const*, void*)>(ctx, OMSetRenderTargets)(ctx, nrt, s.rtv, s.dsv);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetShaderResources)(ctx, 0, 128, s.srvs);
		void* held[] = { s.layout, s.vs, s.ps, s.gs, s.hs, s.ds, s.cb, s.blend, s.depth, s.raster, s.dsv };
		for(int i = 0; i < 11; ++i) Release(held[i]);
		for(int i = 0; i < 8; ++i) Release(s.rtv[i]);
		for(int i = 0; i < 128; ++i) Release(s.srvs[i]);
	}

	// The picture as it is now, copied aside and drawn back through the grade.
	void GradePicture(void* ctx)
	{
		if(!s_back || !s_copyView) return;
		Guard guard;
		void* rtv = NULL;
		ViewDesc rd = { ViewFormat(s_backDesc.Format), 4 /* texture 2D */, 0, 0, 0, 0 };
		// made each time and let go after: a view held on the back buffer would make Civ's resize fail
		if(FAILED(Slot<HRESULT(__stdcall*)(void*, void*, const ViewDesc*, void**)>(s_device, CreateRTV)(s_device, s_back, &rd, &rtv)) || !rtv) return;
		Saved sv;
		Save(ctx, sv);
		void* none = NULL;
		Slot<void(__stdcall*)(void*, void*, void*)>(ctx, CopyResource)(ctx, s_copy, s_back);
		float f[12];
		memcpy(f, s_params, 10 * sizeof(float));
		f[10] = 0; f[11] = s_params[10] - 1;
		Slot<void(__stdcall*)(void*, void*, UINT, const void*, const void*, UINT, UINT)>(ctx, UpdateSubresource)(ctx, s_cb, 0, NULL, f, 0, 0);
		Slot<void(__stdcall*)(void*, void*)>(ctx, IASetInputLayout)(ctx, NULL);
		Slot<void(__stdcall*)(void*, int)>(ctx, IASetPrimitiveTopology)(ctx, 4);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, VSSetShader)(ctx, s_vs, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, PSSetShader)(ctx, s_ps, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, GSSetShader)(ctx, NULL, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, HSSetShader)(ctx, NULL, NULL, 0);
		Slot<void(__stdcall*)(void*, void*, void* const*, UINT)>(ctx, DSSetShader)(ctx, NULL, NULL, 0);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetConstantBuffers)(ctx, 0, 1, &s_cb);
		Slot<void(__stdcall*)(void*, void*, const float*, UINT)>(ctx, OMSetBlendState)(ctx, NULL, NULL, 0xffffffff);
		Slot<void(__stdcall*)(void*, void*, UINT)>(ctx, OMSetDepthStencilState)(ctx, NULL, 0);
		Slot<void(__stdcall*)(void*, void*)>(ctx, RSSetState)(ctx, NULL);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetShaderResources)(ctx, 0, 1, &none);
		Slot<void(__stdcall*)(void*, UINT, void* const*, void*)>(ctx, OMSetRenderTargets)(ctx, 1, &rtv, NULL);
		Viewport vp = { 0, 0, (float)s_backDesc.Width, (float)s_backDesc.Height, 0, 1 };
		Slot<void(__stdcall*)(void*, UINT, const Viewport*)>(ctx, RSSetViewports)(ctx, 1, &vp);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetShaderResources)(ctx, 0, 1, &s_copyView);
		Slot<void(__stdcall*)(void*, UINT, UINT)>(ctx, Draw)(ctx, 3, 0);
		Slot<void(__stdcall*)(void*, UINT, UINT, void* const*)>(ctx, PSSetShaderResources)(ctx, 0, 1, &none);
		Restore(ctx, sv);
		Release(rtv);
		InterlockedIncrement(&s_graded);
	}

	// Keeps our copy the back buffer's size and format; at Present, each frame.
	void FollowBackBuffer(void* swapChain)
	{
		void* back = NULL;
		Slot<HRESULT(__stdcall*)(void*, UINT, const GUID&, void**)>(swapChain, SwapGetBuffer)(swapChain, 0, IID_Texture2D, &back);
		if(!back) return;
		s_back = back;
		Release(back);   // the swap chain keeps it
		int type = 0;
		Slot<void(__stdcall*)(void*, int*)>(back, ResourceGetType)(back, &type);
		if(type != 3) return;
		Tex2DDesc d;
		Slot<void(__stdcall*)(void*, Tex2DDesc*)>(back, TextureGetDesc)(back, &d);
		if(s_copy && d.Width == s_backDesc.Width && d.Height == s_backDesc.Height && d.Format == s_backDesc.Format) return;
		Release(s_copyView); Release(s_copy); s_copyView = s_copy = NULL;
		s_backDesc = d;
		Tex2DDesc cd = d;
		cd.MipLevels = 1; cd.ArraySize = 1; cd.SampleDesc.Count = 1; cd.SampleDesc.Quality = 0;
		cd.Usage = 0; cd.BindFlags = 0x8 /* shader resource */; cd.CPUAccessFlags = 0; cd.MiscFlags = 0;
		ViewDesc vd = { ViewFormat(d.Format), 4 /* texture 2D */, 0, 1, 0, 0 };
		HRESULT a = Slot<HRESULT(__stdcall*)(void*, const Tex2DDesc*, const void*, void**)>(s_device, CreateTexture2D)(s_device, &cd, NULL, &s_copy);
		HRESULT b = s_copy ? Slot<HRESULT(__stdcall*)(void*, void*, const ViewDesc*, void**)>(s_device, CreateSRV)(s_device, s_copy, &vd, &s_copyView) : E_FAIL;
		Note("picture %ux%u, format %u, samples %u: copy 0x%08lx, view 0x%08lx", d.Width, d.Height, d.Format, d.SampleDesc.Count, a, b);
		if(FAILED(a) || FAILED(b) || d.SampleDesc.Count != 1) { Release(s_copyView); Release(s_copy); s_copyView = s_copy = NULL; }
	}

	void* Compile(CompileFn compile, const char* entry, const char* target)
	{
		void* code = NULL;
		void* errors = NULL;
		HRESULT hr = compile(kShader, strlen(kShader), "grade", NULL, NULL, entry, target, 0, 0, &code, &errors);
		if(FAILED(hr))
		{
			Note("compile %s failed 0x%08lx: %s", entry, hr, errors ? (const char*)Slot<void*(__stdcall*)(void*)>(errors, 3)(errors) : "");
			Release(errors);
			return NULL;
		}
		Release(errors);
		return code;
	}

	bool Setup(void* swapChain)
	{
		void* device = NULL;
		if(FAILED(Slot<HRESULT(__stdcall*)(void*, const GUID&, void**)>(swapChain, SwapGetDevice)(swapChain, IID_Device, &device)) || !device)
		{
			Note("GetDevice failed");
			return false;
		}
		Slot<void(__stdcall*)(void*, void**)>(device, GetImmediate)(device, &s_ctx);
		HMODULE comp = LoadLibraryA("d3dcompiler_47.dll");
		if(!comp) comp = LoadLibraryA("d3dcompiler_43.dll");
		CompileFn compile = comp ? (CompileFn)GetProcAddress(comp, "D3DCompile") : NULL;
		if(!compile) { Note("no D3DCompile"); return false; }
		void* vsCode = Compile(compile, "vs", "vs_4_0");
		void* psCode = Compile(compile, "ps", "ps_4_0");
		if(!vsCode || !psCode) { Release(vsCode); Release(psCode); return false; }
		typedef void*(__stdcall* Ptr)(void*);
		typedef SIZE_T(__stdcall* Size)(void*);
		HRESULT a = Slot<HRESULT(__stdcall*)(void*, const void*, SIZE_T, void*, void**)>(device, CreateVS)(device,
			Slot<Ptr>(vsCode, 3)(vsCode), Slot<Size>(vsCode, 4)(vsCode), NULL, &s_vs);
		HRESULT b = Slot<HRESULT(__stdcall*)(void*, const void*, SIZE_T, void*, void**)>(device, CreatePS)(device,
			Slot<Ptr>(psCode, 3)(psCode), Slot<Size>(psCode, 4)(psCode), NULL, &s_ps);
		Release(vsCode);
		Release(psCode);
		BufferDesc bd = { 48, 0 /* default */, 4 /* constant buffer */, 0, 0, 0 };
		HRESULT c = Slot<HRESULT(__stdcall*)(void*, const BufferDesc*, const void*, void**)>(device, CreateBuffer)(device, &bd, NULL, &s_cb);
		if(FAILED(a) || FAILED(b) || FAILED(c)) { Note("shader or buffer creation failed 0x%08lx 0x%08lx 0x%08lx", a, b, c); return false; }
		s_device = device;   // set last: the hooks start once the device is known
		Note("grading pass ready");
		return true;
	}

	typedef void(__stdcall* OmSetFn)(void*, UINT, void* const*, void*);
	typedef void(__stdcall* OmSetUavFn)(void*, UINT, void* const*, void*, UINT, UINT, void* const*, const UINT*);
	OmSetFn s_omSet = NULL;
	OmSetUavFn s_omSetUav = NULL;
	void Mark(void* ctx, UINT n, void* const* rtvs, void* dsv)
	{
		void* back = s_back;
		if(!back || !rtvs || n > 8 || Inside()) return;
		for(UINT i = 0; i < n; ++i)
		{
			if(!rtvs[i]) continue;
			void* res = NULL;
			Slot<void(__stdcall*)(void*, void**)>(rtvs[i], ViewGetResource)(rtvs[i], &res);
			bool isBack = res == back;
			Release(res);
			if(!isBack) continue;
			CtxMarks* m = MarksOf(ctx);
			if(m) { if(dsv) ++m->depth; else ++m->plain; }
			return;
		}
	}
	void __stdcall HookOmSet(void* ctx, UINT n, void* const* rtvs, void* dsv)
	{
		Mark(ctx, n, rtvs, dsv);
		s_omSet(ctx, n, rtvs, dsv);
	}
	void __stdcall HookOmSetUav(void* ctx, UINT n, void* const* rtvs, void* dsv, UINT start, UINT nu, void* const* uavs, const UINT* counts)
	{
		if(n != 0xffffffff) Mark(ctx, n, rtvs, dsv);   // D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL keeps what is bound
		s_omSetUav(ctx, n, rtvs, dsv, start, nu, uavs, counts);
	}

	HRESULT __stdcall HookFinish(void* ctx, BOOL restore, void** list)
	{
		HRESULT hr = s_finish(ctx, restore, list);
		if(SUCCEEDED(hr) && list && *list && !Inside())
		{
			CtxMarks* m = MarksOf(ctx);
			int kind = !m ? kOther : m->depth > 0 ? kWorld : m->plain > 0 ? kFace : kOther;
			if(m) m->depth = m->plain = 0;
			AcquireSRWLockExclusive(&s_ownerLock);
			ListOwner& lo = s_owners[s_ownerNext++ & 4095];
			lo.list = *list; lo.ctx = ctx; lo.kind = kind; lo.frame = s_frame; lo.ran = false;
			ReleaseSRWLockExclusive(&s_ownerLock);
		}
		return hr;
	}

	// A world list made this frame or the last that has not run yet.
	bool WorldPending()
	{
		LONG now = s_frame;
		for(LONG i = 0; i < 4096; ++i)
		{
			const ListOwner& lo = s_owners[i];
			if(lo.list && !lo.ran && lo.kind == kWorld && now - lo.frame <= 1) return true;
		}
		return false;
	}

	// The grade goes in before the first interface list once the frame's world has been drawn and no world list waits.
	void __stdcall HookExecute(void* ctx, void* list, BOOL restore)
	{
		if(s_device && list && !Inside())
		{
			int kind = -1;
			AcquireSRWLockExclusive(&s_ownerLock);
			for(LONG i = 1; i <= 4096 && kind < 0; ++i)   // newest first: Civ reuses list addresses
			{
				ListOwner& lo = s_owners[(s_ownerNext - i) & 4095];
				if(lo.list == list && !lo.ran) { lo.ran = true; kind = lo.kind; }
			}
			bool pending = kind == kFace && WorldPending();
			ReleaseSRWLockExclusive(&s_ownerLock);
			if(kind == kWorld) { s_worldRan = true; if(s_gradedFrame == s_frame) ++s_worldAfter; }
			if(kind == kFace) s_sawFace = true;
			if(kind == kFace && s_mode != 0 && !s_safety.tripped && s_worldRan && s_gradedFrame != s_frame)
			{
				if(pending) ++s_waited;
				else { s_gradedFrame = s_frame; GradePicture(ctx); OrderAdd('|'); }
			}
			OrderAdd(kind == kWorld ? 'w' : kind == kFace ? 'i' : kind == kOther ? 'o' : '?');
		}
		s_exec(ctx, list, restore);
	}

	// ---- the day: moves to the panel's hour in a jump, or glides there over a set time, under s_dayLock ----
	SRWLOCK s_dayLock = SRWLOCK_INIT;
	CgGlide s_glide;
	LARGE_INTEGER s_lastTick, s_freq;

	// ---- cost, logged every 10 s ----
	double s_spanMs = 0, s_spanDt = 0;
	int s_spanFrames = 0;
	void CountCost(double ms, float dt)
	{
		s_spanMs += ms; s_spanDt += dt; ++s_spanFrames;
		if(s_spanDt < 10) return;
		LONG late = InterlockedExchange(&s_late, 0);
		Note("frames (10 s, day %s): %.1f ms apart; plugin at Present %.2f ms; %ld graded before the interface, %ld late; frames with world after the grade %ld, interface held for pending world %ld, no world %ld; last frame: %s",
			s_mode != 0 ? "on" : "off", 1000 * s_spanDt / s_spanFrames, s_spanMs / s_spanFrames, InterlockedExchange(&s_graded, 0) - late, late,
			s_after, s_waited, s_noWorld, s_lastOrder);
		s_after = s_waited = s_noWorld = 0;
		s_spanMs = s_spanDt = 0; s_spanFrames = 0;
	}

	void PresentBody(void* swapChain, float dt)
	{
		AcquireSRWLockExclusive(&s_dayLock);
		float p = CgGlideStep(s_glide, dt);
		ReleaseSRWLockExclusive(&s_dayLock);
		if(s_mode != 0) s_phase = p;
		if(s_broken) return;
		if(!s_device && !Setup(swapChain)) { s_broken = true; return; }
		FollowBackBuffer(swapChain);
		// no interface list after the world: the whole picture is graded now, interface and all
		bool late = s_mode != 0 && !s_safety.tripped && s_worldRan && s_gradedFrame != s_frame;
		if(late) { s_gradedFrame = s_frame; GradePicture(s_ctx); InterlockedIncrement(&s_late); OrderAdd('L'); }
		if(s_mode != 0 && s_worldRan)
		{
			int off = s_safety.off + ((s_worldAfter > 0 || late) ? 1 : 0);
			int r = CgSafetyFrame(s_safety, s_worldAfter > 0 || late);
			if(r == kCgBlip) Note("safety: %d of 120 frames were shaded at a different point (a blip; shading carries on)", off);
			if(r == kCgTrip) Note("safety: shading stopped for this session - %d of 120 frames were shaded at a different point, and it keeps happening (they would flash)", off);
		}
		CgTuning t;
		t.nightDepth = s_nightDepth; t.warmth = s_warmth;
		t.brightness = s_brightness; t.colour = s_colour; t.contrast = s_contrast;
		CgParams c = CgParamsAt(s_phase, t);
		float next[11] = { c.mul[0], c.mul[1], c.mul[2], c.sat, c.sheen[0], c.sheen[1], c.sheen[2], c.k, c.gamma, s_strength, c.contrast };
		memcpy(s_params, next, sizeof(next));   // the next frame's grade
		s_order[s_orderLen] = 0;
		if(s_worldAfter > 0) ++s_after;
		if(!s_worldRan) ++s_noWorld;
		if((s_worldAfter > 0 || late) && s_orderNotes < 40)
		{
			++s_orderNotes;
			Note("frame %ld: %s (%d world lists after the grade)", s_frame, s_order, s_worldAfter);
		}
		memcpy(s_lastOrder, s_order, sizeof(s_order));
		s_orderLen = 0; s_worldAfter = 0; s_worldRan = false;
		InterlockedIncrement(&s_frame);
	}

	HRESULT __stdcall HookPresent(void* swapChain, UINT sync, UINT flags)
	{
		{
			Guard guard;
			LARGE_INTEGER a, b;
			QueryPerformanceCounter(&a);
			float dt = s_lastTick.QuadPart ? (float)(a.QuadPart - s_lastTick.QuadPart) / (float)s_freq.QuadPart : 0;
			s_lastTick = a;
			if(dt > 0.25f) dt = 0.25f;
			PresentBody(swapChain, dt);
			QueryPerformanceCounter(&b);
			if(s_freq.QuadPart) CountCost(1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)s_freq.QuadPart, dt);
		}
		return s_present(swapChain, sync, flags);
	}

	bool Patch(Vt vt, int slot, void* fn, void** original)
	{
		DWORD old = 0;
		if(!VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
		*original = vt[slot];
		vt[slot] = fn;
		VirtualProtect(&vt[slot], sizeof(void*), old, &old);
		return true;
	}
	void PutBack(Vt vt, int slot, void* original)
	{
		if(!vt || !original) return;
		DWORD old = 0;
		if(!VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		vt[slot] = original;
		VirtualProtect(&vt[slot], sizeof(void*), old, &old);
	}

	// A throwaway device gives the vtables the game's swap chain, immediate context and deferred contexts share.
	void InstallHooks()
	{
		s_tls = TlsAlloc();
		QueryPerformanceFrequency(&s_freq);
		s_lastTick.QuadPart = 0;
		HMODULE d3d = LoadLibraryA("d3d11.dll");
		CreateDeviceAndSwapChainFn create = d3d ? (CreateDeviceAndSwapChainFn)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain") : NULL;
		if(!create) { Note("no D3D11CreateDeviceAndSwapChain"); return; }
		HWND wnd = CreateWindowExA(0, "STATIC", "colorgrade", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 1;
		sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = wnd;
		sd.SampleDesc.Count = 1;
		sd.Windowed = TRUE;
		void* sc = NULL; void* dev = NULL; void* ctx = NULL; int level = 0;
		HRESULT hr = create(NULL, 1, NULL, 0, NULL, 0, 7, &sd, &sc, &dev, &level, &ctx);
		if(FAILED(hr) || !sc) { Note("throwaway swap chain failed 0x%08lx", hr); if(wnd) DestroyWindow(wnd); return; }
		void* def = NULL;
		HRESULT dh = Slot<HRESULT(__stdcall*)(void*, UINT, void**)>(dev, CreateDeferred)(dev, 0, &def);
		Vt defVt = SUCCEEDED(dh) && def ? VT(def) : VT(ctx);
		if(Patch(VT(ctx), ExecuteCommandList, (void*)&HookExecute, (void**)&s_exec)) s_execVt = VT(ctx);
		if(Patch(defVt, FinishCommandList, (void*)&HookFinish, (void**)&s_finish)) s_finishVt = defVt;
		if(defVt != VT(ctx))   // the immediate context's binds (the grade's own) are never watched
		{
			if(Patch(defVt, OMSetRenderTargets, (void*)&HookOmSet, (void**)&s_omSet)) s_omVt = defVt;
			Patch(defVt, OMSetRenderTargetsAndUAV, (void*)&HookOmSetUav, (void**)&s_omSetUav);
		}
		Release(def);
		if(Patch(VT(sc), SwapPresent, (void*)&HookPresent, (void**)&s_present)) { s_swapVt = VT(sc); Note("Present hooked (feature level 0x%x)", level); }
		else Note("could not patch Present");
		Note("command list hooks in: deferred contexts %s (0x%08lx)", defVt != VT(ctx) ? "separate" : "shared", dh);
		Release(ctx); Release(dev); Release(sc);
		DestroyWindow(wnd);
	}
}

void CgInstallPresentHook() { InstallHooks(); }

void CgRestorePresent()
{
	// Bazaar unloads plugins when the mods change; nothing may point into an unloaded DLL.
	PutBack(s_swapVt, SwapPresent, (void*)s_present);
	PutBack(s_execVt, ExecuteCommandList, (void*)s_exec);
	PutBack(s_finishVt, FinishCommandList, (void*)s_finish);
	PutBack(s_omVt, OMSetRenderTargets, (void*)s_omSet);
	PutBack(s_omVt, OMSetRenderTargetsAndUAV, (void*)s_omSetUav);
	Note("shutdown: hooks put back");
}

void CgNote(const char* text) { Note("%s", text); }

void CgSet(int mode, float daySeconds, float strength, float nightDepth, float warmth, float brightness, float colour, float contrast)
{
	(void)daySeconds;
	s_brightness = brightness < 0.25f ? 0.25f : brightness > 2 ? 2 : brightness;
	s_colour = colour < 0 ? 0 : colour > 2 ? 2 : colour;
	s_contrast = contrast < 0.25f ? 0.25f : contrast > 2 ? 2 : contrast;
	s_strength = strength < 0 ? 0 : strength > 1 ? 1 : strength;
	s_nightDepth = nightDepth < 0 ? 0 : nightDepth > 1 ? 1 : nightDepth;
	s_warmth = warmth < 0 ? 0 : warmth > 1 ? 1 : warmth;
	s_mode = mode;
}

void CgSetTarget(float phase, float seconds)
{
	AcquireSRWLockExclusive(&s_dayLock);
	s_target = phase - (float)(int)phase;
	CgGlideTo(s_glide, s_phase, s_target, seconds);
	if(seconds <= 0) s_phase = s_target;
	ReleaseSRWLockExclusive(&s_dayLock);
}

void CgLand(float withinSeconds)
{
	AcquireSRWLockExclusive(&s_dayLock);
	CgGlideLand(s_glide, withinSeconds);
	ReleaseSRWLockExclusive(&s_dayLock);
}

float CgPhase() { return s_phase; }
void CgSetPhase(float phase)
{
	AcquireSRWLockExclusive(&s_dayLock);
	s_phase = phase - (float)(int)phase;
	CgGlideTo(s_glide, s_phase, s_phase, 0);
	ReleaseSRWLockExclusive(&s_dayLock);
}

const char* CgStatus()
{
	if(s_broken) return "the shader could not be set up (see Logs\\ColorGrade.log)";
	if(!s_device) return "waiting for the first frame";
	if(!s_copyView) return "the screen could not be copied (see Logs\\ColorGrade.log)";
	if(s_safety.tripped) return "stopped: the shade would flash (see Logs\\ColorGrade.log)";
	if(!s_sawFace) return "looking for the interface";
	return "shading the map each frame";
}
