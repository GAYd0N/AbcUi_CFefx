#pragma once

// abc::Render - AbcUi-owned IMetaRenderer bridge + draw helpers.
// Ported from ABCEnchance src/core/metahook/MetaRenderer.* and the MetaRenderer
// half of src/core/opengl/gl_draw.*. Renamed away from "gl_": this module makes
// no OpenGL calls - everything goes through IMetaRenderer (Renderer.dll).
//
// This is UI-owned renderer glue: AbcCore has no Renderer headers, submodule or
// build-time dependency. AbcUi owns the direct Renderer dependency.

#ifndef ABC_RENDER_H
#define ABC_RENDER_H

#include <IMetaRenderer.h>

#include "RenderCommon.h"

namespace abc {

// Renderer.dll interface, resolved lazily by MetaRendererInit(). Null until
// init succeeds (Renderer plugin absent).
IMetaRenderer *MetaRenderer();
// Locates the Renderer plugin through the MetaHook plugin registry and grabs
// METARENDERER_INTERFACE_VERSION. Fatals via abc::SysError when missing.
void MetaRendererInit();
void MetaRendererShutdown();

// Legacy GL_Bind: bind an engine texture id through the renderer.
void BindTexture(int gltexturenum);

// Full-control textured quad (positions p1..p4 = TL, BL, BR, TR in screen
// space; uv spans 0..1). mode kRenderTransAdd selects additive blending.
void DrawTexturePos(int tex, int mode, float p1[2], float p2[2], float p3[2],
                    float p4[2], unsigned char r, unsigned char g,
                    unsigned char b, unsigned char a);

// Shader compilation through the renderer; paths are mod-relative
// ("abcui/shader/x.vsh"). Returns a program handle usable with
// IMetaRenderer (0 on failure).
unsigned int CompileShaderFile(const char *vsfile, const char *fsfile);

} // namespace abc

#endif // ABC_RENDER_H
