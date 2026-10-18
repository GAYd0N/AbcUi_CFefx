// framework::texture implementation - see framework/TextureSink.h.
#include "framework/TextureSink.h"

#include "VGUI2ExtensionImport.h"

namespace framework::texture {

bool available() {
  return vgui2ext::available();
}

int create() {
  return vgui2ext::createTexture();
}

void upload(int id, const unsigned char *rgba, int width, int height) {
  if (id)
    vgui2ext::uploadTexture(id, rgba, width, height);
}

void release(int id) {
  if (id)
    vgui2ext::deleteTextureById(id);
}

} // namespace framework::texture
