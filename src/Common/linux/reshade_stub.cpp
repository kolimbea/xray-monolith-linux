// ReShade add-on hooks (ReShadeCompat/addon.cpp) need ReShade's Windows DLL injection; there is no
// ReShade on native Linux, so registration fails exactly as on Windows when ReShade is not installed.
bool init_reshade() { return false; }
void unregister_reshade() {}
void render_reshade_effects() {}
