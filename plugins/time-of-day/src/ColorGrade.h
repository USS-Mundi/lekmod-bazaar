// Time of day's Direct3D side (ColorGradeD3D.cpp), called from the plugin's entry, shutdown and Lua functions.
#pragma once
void CgInstallPresentHook();
void CgRestorePresent();
void CgNote(const char* text);
// mode 0 off, otherwise the day sits at the target phase (set with CgSetTarget). daySeconds is no longer used (kept for
// the panel's call). strength, nightDepth and warmth in [0, 1]; brightness, colour and contrast are factors (1 as made).
void CgSet(int mode, float daySeconds, float strength, float nightDepth, float warmth, float brightness, float colour, float contrast);
// The day goes to phase: at once (seconds 0), or gliding there over seconds, easing in and out.
void CgSetTarget(float phase, float seconds);
// A glide still under way lands within withinSeconds (sooner if it was due sooner).
void CgLand(float withinSeconds);
float CgPhase();
void CgSetPhase(float phase);
// What the plugin is doing, for the panel.
const char* CgStatus();
