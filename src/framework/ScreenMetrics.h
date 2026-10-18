#pragma once

// framework screen metrics - the current swap-chain size, with the historical
// fallbacks used while the renderer interface is unavailable. Data/framework
// code uses these instead of including render/Render.h.

namespace framework {

int ScreenWidth();
int ScreenHeight();
void ScreenSize(int &width, int &height);

} // namespace framework
