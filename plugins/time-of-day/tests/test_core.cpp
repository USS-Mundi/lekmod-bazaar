// Native tests for the time-of-day grades. Run: tests/run_core.sh
#include "../src/CgLooks.h"
#include <math.h>
#include <stdio.h>

static int g_run = 0, g_failed = 0;
#define CHECK(c) do { ++g_run; if(!(c)) { ++g_failed; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while(0)
static bool Near(float a, float b) { return fabsf(a - b) < 1e-4f; }
static bool IsDay(const CgParams& p) { return Near(p.sat, 1) && Near(p.mul[0], 1) && Near(p.mul[2], 1) && Near(p.k, 0) && Near(p.gamma, 1) && Near(p.contrast, 1); }

int main()
{
	CgTuning t;
	CHECK(IsDay(CgParamsAt(0.0f, t)));
	CHECK(IsDay(CgParamsAt(1.0f, t)));
	// every look sits exactly at its time
	CHECK(Near(CgParamsAt(0.20f, t).mul[2], 0.60f));          // golden hour
	CHECK(Near(CgParamsAt(0.72f, t).mul[1], 0.78f));          // dawn
	// a gradient through the whole cycle: no stretch of it holds still (the old table held day for 30% of the day and
	// night for 24%, and stopped at every look)
	float stillest = 1;
	for(int i = 0; i < 1000; ++i)
	{
		CgParams a = CgParamsAt(i / 1000.0f, t), b = CgParamsAt(i / 1000.0f + 0.04f, t);
		float d = 0;
		for(int c = 0; c < 3; ++c) { float e = fabsf(a.mul[c] - b.mul[c]); if(e > d) d = e; }
		float e = fabsf(a.sat - b.sat); if(e > d) d = e;
		if(d < stillest) stillest = d;
	}
	CHECK(stillest > 0.004f);
	// no overshoot past the looks: each channel stays within the range the looks span
	float lo = 1, hi = 0;
	for(int i = 0; i < 1000; ++i) { float m = CgParamsAt(i / 1000.0f, t).mul[2]; if(m < lo) lo = m; if(m > hi) hi = m; }
	CHECK(lo >= 0.60f - 1e-4f && hi <= 1.0f + 1e-4f);
	CgParams night = CgParamsAt(0.5f, t);
	CHECK(Near(night.mul[0], 0.42f) && Near(night.mul[2], 0.80f) && Near(night.sat, 0.55f));
	CHECK(Near(CgParamsAt(0.28f, t).mul[1], 0.55f));          // dusk at its key
	// smooth: neighbouring frames differ by little
	float worst = 0;
	for(int i = 0; i < 10000; ++i)
	{
		CgParams a = CgParamsAt(i / 10000.0f, t), b = CgParamsAt((i + 1) / 10000.0f, t);
		for(int c = 0; c < 3; ++c) { float d = fabsf(a.mul[c] - b.mul[c]); if(d > worst) worst = d; }
	}
	CHECK(worst < 0.002f);
	// night depth 0: night is day; warmth 0: dusk is a plain fade (no red push)
	CgTuning flat; flat.nightDepth = 0; flat.warmth = 1;
	CHECK(IsDay(CgParamsAt(0.5f, flat)));
	CgTuning cool; cool.warmth = 0;
	CgParams d = CgParamsAt(0.28f, cool);
	CHECK(d.mul[0] < 1 && d.mul[2] > d.mul[0]);                // fading toward night, blue holds more than red
	CHECK(Near(CgPhaseOf("night"), 0.5f) && CgPhaseOf("noon") < 0);
	CHECK(CgPartOf(0.5f)[0] == 'N' && CgPartOf(0.05f)[0] == 'D' && CgPartOf(0.75f)[1] == 'a');
	// brightness, colour and contrast tune the looks from golden hour through night to dawn; noon stays the game's own
	{
		CgTuning base, tuned;
		tuned.brightness = 1.2f; tuned.colour = 0.5f; tuned.contrast = 1.3f;
		CgParams n0 = CgParamsAt(0.5f, base), n1 = CgParamsAt(0.5f, tuned);
		CHECK(Near(n1.mul[0], n0.mul[0] * 1.2f) && Near(n1.mul[2], n0.mul[2] * 1.2f));
		CHECK(Near(n1.sat, n0.sat * 0.5f));
		CHECK(Near(n0.contrast, 1) && Near(n1.contrast, 1.3f));
		CHECK(IsDay(CgParamsAt(0, tuned)) && Near(CgParamsAt(0, tuned).contrast, 1));
		CgParams g1 = CgParamsAt(0.20f, tuned), g0 = CgParamsAt(0.20f, base);
		CHECK(Near(g1.contrast, 1.3f) && Near(g1.mul[1], g0.mul[1] * 1.2f));   // full from golden hour on
		CgParams m = CgParamsAt(0.14f, tuned);
		CHECK(m.contrast > 1 && m.contrast < 1.3f);                               // easing in through the afternoon
	}
	// the glide: forward to the next hour over the time given, easing in and out
	{
		CgGlide g;
		CgGlideTo(g, 0.95f, 0.05f, 2.0f);                     // across noon: forward through 1, not back
		float last = 0.95f, v = 0, top = 0;
		bool forward = true;
		for(int i = 0; i < 200; ++i)
		{
			float p = CgGlideStep(g, 0.01f);
			float step = p - last; if(step < -0.5f) step += 1;
			if(step < -1e-6f) forward = false;
			if(i == 0) v = step;
			if(step > top) top = step;
			last = p;
		}
		CHECK(forward);
		CHECK(Near(last, 0.05f) && CgGlideDone(g));
		CHECK(v < top * 0.1f);                                 // starts gently, not in a snap
		CHECK(Near(CgGlideStep(g, 0.5f), 0.05f));              // and stays there
	}
	{
		// landing early: the rest of the glide is squeezed into the time given, keeping its speed (no jolt)
		CgGlide g;
		CgGlideTo(g, 0.0f, 0.1f, 4.0f);
		float a = 0, b = 0;
		for(int i = 0; i < 100; ++i) { a = b; b = CgGlideStep(g, 0.01f); }
		float before = b - a;
		CgGlideLand(g, 0.5f);
		float c = CgGlideStep(g, 0.01f);
		CHECK(c - b >= before * 0.9f);                         // carries on at least as fast
		for(int i = 0; i < 49; ++i) c = CgGlideStep(g, 0.01f);
		CHECK(Near(c, 0.1f) && CgGlideDone(g));                // there within the half second
		CgGlide h;
		CgGlideTo(h, 0.0f, 0.1f, 1.0f);
		CgGlideLand(h, 3.0f);                                  // landing later than planned changes nothing
		for(int i = 0; i < 100; ++i) CgGlideStep(h, 0.01f);
		CHECK(CgGlideDone(h));
		CgGlide j;
		CgGlideTo(j, 0.3f, 0.4f, 0);                           // no time: a jump
		CHECK(Near(CgGlideStep(j, 0.01f), 0.4f));
	}

	// the safety: a lone blip is reported and shading carries on; only a shade that keeps flashing stops it
	{
		CgSafety s;
		for(int i = 0; i < 300; ++i) CHECK(CgSafetyFrame(s, true) == kCgCalm);    // settling: ignored
		int r = kCgCalm;
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, i == 5);
		CHECK(r == kCgBlip && !s.tripped);                                         // one off frame in 120
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, false);
		CHECK(r == kCgCalm);                                                       // a clean window clears the run
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, i == 7);
		CHECK(r == kCgBlip);
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, i == 9);
		CHECK(r == kCgBlip && !s.tripped);                                         // two windows in a row: still on
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, i == 3);
		CHECK(r == kCgTrip && s.tripped);                                          // three in a row: it keeps flashing
		for(int i = 0; i < 240; ++i) r = CgSafetyFrame(s, false);
		CHECK(s.tripped && r == kCgCalm);                                          // stays stopped
	}
	{
		CgSafety s; s.settle = 0;
		int r = kCgCalm;
		for(int i = 0; i < 120; ++i) r = CgSafetyFrame(s, i % 10 == 0);           // 12 of 120: a strobe
		CHECK(r == kCgTrip && s.tripped);
		CgSafety a; a.settle = 0;
		for(int i = 0; i < 360; ++i) r = CgSafetyFrame(a, true);                  // every frame the same: steady, no flash
		CHECK(!a.tripped && r == kCgCalm);
	}

	printf("%d/%d passed\n", g_run - g_failed, g_run);
	return g_failed ? 1 : 0;
}
