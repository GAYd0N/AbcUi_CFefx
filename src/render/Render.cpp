// AbcUi-owned IMetaRenderer bridge.
// Ported from ABCEnchance src/core/metahook/MetaRenderer.cpp and the
// MetaRenderer-callable parts of src/core/opengl/gl_draw.cpp / gl_def.cpp.
// SYS_ERROR -> abc::SysError; pp_colorlize (domain shader state) stays with
// the feature plugin that owns it.
#include <metahook.h>

#include "Render.h"
#include <abc/Plugin.h>

static HINTERFACEMODULE g_hMetaRenderer = nullptr;
static IMetaRenderer *g_pMetaRenderer = nullptr;

namespace abc {

IMetaRenderer *MetaRenderer() {
  return g_pMetaRenderer;
}

void MetaRendererInit() {
  mh_plugininfo_t rendererInfo{};
  if (!g_pMetaHookAPI->GetPluginInfoByBaseFileName("Renderer", &rendererInfo)) {
    SysError("Could not get Renderer");
    return;
  }
  g_hMetaRenderer = (HINTERFACEMODULE)rendererInfo.PluginModuleBase;
  auto factory = Sys_GetFactory(g_hMetaRenderer);
  if (!factory) {
    SysError("Could not get factory from Renderer");
    return;
  }
  g_pMetaRenderer = (decltype(g_pMetaRenderer))factory(
      METARENDERER_INTERFACE_VERSION, nullptr);
  if (!g_pMetaRenderer) {
    SysError("Could not get interface from Renderer");
    return;
  }
}

void MetaRendererShutdown() {
  // Never unload by ourself! Only the module-handle bookkeeping is dropped;
  // g_pMetaRenderer intentionally stays - the Renderer module is owned by
  // the host, and teardown paths may still call MetaRenderer() after this.
  if (g_hMetaRenderer) {
    g_hMetaRenderer = nullptr;
  }
}

void BindTexture(int gltexturenum) {
  if (MetaRenderer()) {
    MetaRenderer()->Bind(gltexturenum);
  }
}

void DrawTexturePos(int tex, int mode, float p1[2], float p2[2], float p3[2],
                    float p4[2], unsigned char r, unsigned char g,
                    unsigned char b, unsigned char a) {
  if (MetaRenderer()) {
    uint64_t programState = DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED;
    if (mode == kRenderTransAdd) {
      programState = DRAW_TEXTURED_RECT_ALPHA_BASED_ADDITIVE_ENABLED;
    }
    float color4v[4]{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    texturedrectvertex_t vertices[4];
    vertices[0].pos[0] = p1[0];
    vertices[0].pos[1] = p1[1];
    vertices[0].texcoord[0] = 0.0f;
    vertices[0].texcoord[1] = 0.0f;
    vertices[0].col[0] = color4v[0];
    vertices[0].col[1] = color4v[1];
    vertices[0].col[2] = color4v[2];
    vertices[0].col[3] = color4v[3];

    vertices[1].pos[0] = p2[0];
    vertices[1].pos[1] = p2[1];
    vertices[1].texcoord[0] = 0.0f;
    vertices[1].texcoord[1] = 1.0f;
    vertices[1].col[0] = color4v[0];
    vertices[1].col[1] = color4v[1];
    vertices[1].col[2] = color4v[2];
    vertices[1].col[3] = color4v[3];

    vertices[2].pos[0] = p3[0];
    vertices[2].pos[1] = p3[1];
    vertices[2].texcoord[0] = 1.0f;
    vertices[2].texcoord[1] = 1.0f;
    vertices[2].col[0] = color4v[0];
    vertices[2].col[1] = color4v[1];
    vertices[2].col[2] = color4v[2];
    vertices[2].col[3] = color4v[3];

    vertices[3].pos[0] = p4[0];
    vertices[3].pos[1] = p4[1];
    vertices[3].texcoord[0] = 1.0f;
    vertices[3].texcoord[1] = 0.0f;
    vertices[3].col[0] = color4v[0];
    vertices[3].col[1] = color4v[1];
    vertices[3].col[2] = color4v[2];
    vertices[3].col[3] = color4v[3];
    uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    MetaRenderer()->DrawTexturedRect(tex, vertices, 4, indices, 6, programState,
                                     "abc::DrawTexturePos");
  }
}

unsigned int CompileShaderFile(const char *vsfile, const char *fsfile) {
  if (MetaRenderer())
    return MetaRenderer()->CompileShaderFile(vsfile, fsfile);
  return 0;
}

} // namespace abc
