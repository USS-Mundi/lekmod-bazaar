// The time-of-day grades and when to apply them, with no game or Direct3D headers so they can be tested natively
// (tests/run_core.sh). ColorGradeD3D.cpp hands the result to the pixel shader each frame.
#pragma once

// One grade, as the shader applies it to a pixel c: l = luminance(c); a = l + (c - l) * sat; a = a * mul + k * l^2 *
// sheen; a = (a - 0.5) * contrast + 0.5; a = saturate(a) ^ gamma; out = lerp(c, a, strength). Day is the identity.
struct CgParams
{
	float sat;
	float mul[3];
	float sheen[3];
	float k;
	float gamma;
	float contrast;     // set by CgParamsAt (the looks' own tables leave it out)
};

// The settings a player tunes: how dark night gets (0 none, 1 full) and how warm dawn, golden hour and dusk are (0 a
// plain fade between day and night, 1 full colour).
// Brightness, colour (saturation) and contrast scale the looks from golden hour through night to dawn (1 leaves them
// as made); they ease in through the afternoon and morning, so noon stays the game's own colours.
struct CgTuning
{
	float nightDepth;
	float warmth;
	float brightness, colour, contrast;
	CgTuning() : nightDepth(1), warmth(1), brightness(1), colour(1), contrast(1) {}
};

// The grade at a point of the day: phase 0 is noon, golden hour 0.20, dusk 0.28, night 0.5 (midnight), dawn 0.72, and
// back to noon at 1. A gradient through the whole cycle: exactly each look at its time, always moving in between.
CgParams CgParamsAt(float phase, const CgTuning& tuning);
// Where a named look sits in the day: "day", "golden", "dusk", "night", "dawn"; -1 for an unknown name.
float CgPhaseOf(const char* look);
// The part of the day a phase falls in, as a word for the panel.
const char* CgPartOf(float phase);

// A glide of the day from one phase to the next, always forward (through noon at 1 when it must), over a set time:
// it eases in from rest and settles at the target. Landing early squeezes what is left into a shorter time, carrying
// on from the speed it has.
struct CgGlide
{
	float from, to;     // to is unwrapped: from <= to < from + 1
	float speed;        // phase per second at the start
	float seconds, t;
	CgGlide() : from(0), to(0), speed(0), seconds(0), t(0) {}
};
void CgGlideTo(CgGlide& g, float current, float target, float seconds);
void CgGlideLand(CgGlide& g, float withinSeconds);
float CgGlideStep(CgGlide& g, float dt);     // advances by dt and returns the phase, in [0, 1)
bool CgGlideDone(const CgGlide& g);

// The safety against a flashing shade. A frame is "off" when it was shaded at a different point from its neighbours.
// Counted in windows of 120 frames with a world, after the first `settle` while the game settles. A window with some,
// but not all, frames off is a blip; shading stops for the session (tripped) when a window has 12 or more off, or
// three windows in a row blip. Every frame off is steady, not a flash.
enum { kCgCalm, kCgBlip, kCgTrip };
struct CgSafety
{
	int settle, frames, off, blips;
	bool tripped;
	CgSafety() : settle(300), frames(0), off(0), blips(0), tripped(false) {}
};
// Counts one frame with a world; returns kCgBlip or kCgTrip on the frame that closes such a window, else kCgCalm.
int CgSafetyFrame(CgSafety& s, bool off);

