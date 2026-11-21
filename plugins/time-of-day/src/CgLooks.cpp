#include "CgLooks.h"

#include <math.h>
#include <string.h>

namespace
{
	const CgParams kDay    = { 1.00f, { 1.00f, 1.00f, 1.00f }, { 0.0f, 0.0f, 0.0f }, 0.00f, 1.00f };
	const CgParams kGolden = { 1.05f, { 1.05f, 0.86f, 0.60f }, { 1.0f, 0.80f, 0.45f }, 0.20f, 0.98f };
	const CgParams kDusk   = { 0.75f, { 0.80f, 0.55f, 0.62f }, { 1.0f, 0.55f, 0.35f }, 0.16f, 1.02f };
	const CgParams kNight  = { 0.55f, { 0.42f, 0.52f, 0.80f }, { 0.6f, 0.70f, 1.00f }, 0.10f, 0.95f };
	const CgParams kDawn   = { 0.80f, { 0.92f, 0.78f, 0.86f }, { 1.0f, 0.75f, 0.80f }, 0.18f, 1.00f };

	float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	CgParams Mix(const CgParams& a, const CgParams& b, float t)
	{
		CgParams r;
		r.sat = Lerp(a.sat, b.sat, t);
		for(int i = 0; i < 3; ++i) { r.mul[i] = Lerp(a.mul[i], b.mul[i], t); r.sheen[i] = Lerp(a.sheen[i], b.sheen[i], t); }
		r.k = Lerp(a.k, b.k, t);
		r.gamma = Lerp(a.gamma, b.gamma, t);
		return r;
	}

	float Wrap(float p) { p = p - floorf(p); return p >= 1 ? 0 : p; }
}

// A periodic monotone cubic through the looks: it passes through each look at its time, never stops between them, and
// never overshoots past them (each value turns only where the looks themselves turn).
static float Through(const float* y, const float* x, int n, float p)
{
	int i = n - 1;
	for(int k = 0; k < n; ++k) if(p >= x[k] && (k + 1 == n || p < x[k + 1])) { i = k; break; }
	if(p < x[0]) i = n - 1;
	int j = (i + 1) % n;
	float x0 = x[i], x1 = j == 0 ? x[0] + 1 : x[j];
	float pp = p < x0 ? p + 1 : p;
	float h = x1 - x0, t = (pp - x0) / h;
	// slopes of the segments around each end, wrapping round the day
	float slope[2];
	for(int e = 0; e < 2; ++e)
	{
		int k = e == 0 ? i : j;
		int a = (k + n - 1) % n, b = (k + 1) % n;
		float ha = x[k] - x[a]; if(ha <= 0) ha += 1;
		float hb = x[b] - x[k]; if(hb <= 0) hb += 1;
		float da = (y[k] - y[a]) / ha, db = (y[b] - y[k]) / hb;
		if(da * db <= 0) slope[e] = 0;
		else { float w1 = 2 * hb + ha, w2 = hb + 2 * ha; slope[e] = (w1 + w2) / (w1 / da + w2 / db); }
	}
	float t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * h * slope[0] + (-2 * t3 + 3 * t2) * y[j] + (t3 - t2) * h * slope[1];
}

CgParams CgParamsAt(float phase, const CgTuning& tuning)
{
	// The tuned night, and the in-between looks pulled toward a plain fade from day to that night as warmth falls.
	CgParams night = Mix(kDay, kNight, tuning.nightDepth);
	CgParams golden = Mix(Mix(kDay, night, 0.15f), kGolden, tuning.warmth);
	CgParams dusk = Mix(Mix(kDay, night, 0.45f), kDusk, tuning.warmth);
	CgParams dawn = Mix(Mix(kDay, night, 0.35f), kDawn, tuning.warmth);
	const int n = 5;
	const float x[n] = { 0.00f, 0.20f, 0.28f, 0.50f, 0.72f };
	const CgParams* look[n] = { &kDay, &golden, &dusk, &night, &dawn };
	float p = Wrap(phase);
	CgParams r;
	float y[n];
#define CG_CHANNEL(field) do { for(int k = 0; k < n; ++k) y[k] = look[k]->field; r.field = Through(y, x, n, p); } while(0)
	CG_CHANNEL(sat);
	CG_CHANNEL(mul[0]); CG_CHANNEL(mul[1]); CG_CHANNEL(mul[2]);
	CG_CHANNEL(sheen[0]); CG_CHANNEL(sheen[1]); CG_CHANNEL(sheen[2]);
	CG_CHANNEL(k);
	CG_CHANNEL(gamma);
#undef CG_CHANNEL
	// the player's brightness, colour and contrast: full from golden hour to dawn, none at noon
	float d = p < 0.5f ? p : 1 - p;
	float s = d <= 0.08f ? 0 : d >= 0.20f ? 1 : (d - 0.08f) / 0.12f;
	float w = s * s * (3 - 2 * s);
	for(int c = 0; c < 3; ++c) r.mul[c] *= 1 + (tuning.brightness - 1) * w;
	r.sat *= 1 + (tuning.colour - 1) * w;
	r.contrast = 1 + (tuning.contrast - 1) * w;
	return r;
}

float CgPhaseOf(const char* look)
{
	if(!look) return -1;
	if(strcmp(look, "day") == 0) return 0.0f;
	if(strcmp(look, "golden") == 0) return 0.20f;
	if(strcmp(look, "dusk") == 0) return 0.28f;
	if(strcmp(look, "night") == 0) return 0.50f;
	if(strcmp(look, "dawn") == 0) return 0.72f;
	return -1;
}

const char* CgPartOf(float phase)
{
	float p = Wrap(phase);
	if(p < 0.15f || p >= 0.80f) return "Day";
	if(p < 0.24f) return "Golden hour";
	if(p < 0.34f) return "Dusk";
	if(p < 0.66f) return "Night";
	return "Dawn";
}

// ---- the glide: a cubic from the phase it starts at (with the speed it has) to rest at the target ----
namespace
{
	// the speed a glide may start with and still never pass its target (a cubic overshoots above 3x the mean speed)
	float Capped(float speed, float distance, float seconds)
	{
		float most = seconds > 0 ? 3 * distance / seconds : 0;
		return speed < 0 ? 0 : speed > most ? most : speed;
	}
	void At(const CgGlide& g, float* p, float* v)
	{
		if(CgGlideDone(g)) { *p = g.to; *v = 0; return; }
		float s = g.t / g.seconds, s2 = s * s, s3 = s2 * s, d = g.seconds;
		*p = (2 * s3 - 3 * s2 + 1) * g.from + (s3 - 2 * s2 + s) * d * g.speed + (-2 * s3 + 3 * s2) * g.to;
		*v = ((6 * s2 - 6 * s) * g.from + (3 * s2 - 4 * s + 1) * d * g.speed + (-6 * s2 + 6 * s) * g.to) / d;
	}
}

void CgGlideTo(CgGlide& g, float current, float target, float seconds)
{
	current = Wrap(current);
	float delta = Wrap(target - current);
	if(delta > 0.9999f) delta = 0;
	g.from = current; g.to = current + delta; g.speed = 0; g.t = 0;
	g.seconds = seconds > 0 && delta > 0 ? seconds : 0;
}

void CgGlideLand(CgGlide& g, float withinSeconds)
{
	if(CgGlideDone(g) || g.seconds - g.t <= withinSeconds) return;
	float p, v;
	At(g, &p, &v);
	if(withinSeconds <= 0) { g.from = g.to; g.seconds = 0; g.t = 0; return; }
	g.from = p; g.speed = Capped(v, g.to - p, withinSeconds); g.seconds = withinSeconds; g.t = 0;
}

float CgGlideStep(CgGlide& g, float dt)
{
	if(!CgGlideDone(g)) g.t += dt;
	float p, v;
	At(g, &p, &v);
	return Wrap(p);
}

bool CgGlideDone(const CgGlide& g) { return g.seconds <= 0 || g.t >= g.seconds - 1e-4f; }

int CgSafetyFrame(CgSafety& s, bool off)
{
	if(s.tripped) return kCgCalm;
	if(s.settle > 0) { --s.settle; return kCgCalm; }
	if(off) ++s.off;
	if(++s.frames < 120) return kCgCalm;
	int n = s.off;
	s.frames = s.off = 0;
	if(n == 0 || n == 120) { s.blips = 0; return kCgCalm; }
	if(n >= 12 || ++s.blips >= 3) { s.tripped = true; return kCgTrip; }
	return kCgBlip;
}
