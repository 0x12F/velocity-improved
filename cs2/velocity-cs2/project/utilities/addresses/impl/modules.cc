module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace addresses::modules {

bool Initialize() {
  client = MODULE_BASE("client.dll");
  engine2 = MODULE_BASE("engine2.dll");
  server = MODULE_BASE("server.dll");
  scene_system = MODULE_BASE("scenesystem.dll");
  material_system2 = MODULE_BASE("materialsystem2.dll");
  render_system_dx11 = MODULE_BASE("rendersystemdx11.dll");
  panorama = MODULE_BASE("panorama.dll");
  game_overlay_renderer = MODULE_BASE("gameoverlayrenderer64.dll");
  schema_system = MODULE_BASE("schemasystem.dll");
  input_system = MODULE_BASE("inputsystem.dll");
  sound_system = MODULE_BASE("soundsystem.dll");
  tier0 = MODULE_BASE("tier0.dll");
  particles = MODULE_BASE("particles.dll");
  resource_system = MODULE_BASE("resourcesystem.dll");
  localize = MODULE_BASE("localize.dll");
  mesh_system = MODULE_BASE("meshsystem.dll");
  file_system_stdio = MODULE_BASE("filesystem_stdio.dll");
  vphysics2 = MODULE_BASE("vphysics2.dll");

  logging::console::Print("[modules] client:              {:#x}", client);
  logging::console::Print("[modules] engine2:             {:#x}", engine2);
  logging::console::Print("[modules] server:              {:#x}", server);
  logging::console::Print("[modules] scene_system:        {:#x}", scene_system);
  logging::console::Print("[modules] material_system2:    {:#x}",
                          material_system2);
  logging::console::Print("[modules] render_system_dx11:  {:#x}",
                          render_system_dx11);
  logging::console::Print("[modules] panorama:            {:#x}", panorama);
  logging::console::Print("[modules] game_overlay:        {:#x}",
                          game_overlay_renderer);
  logging::console::Print("[modules] schema_system:       {:#x}",
                          schema_system);
  logging::console::Print("[modules] input_system:        {:#x}", input_system);
  logging::console::Print("[modules] sound_system:        {:#x}", sound_system);
  logging::console::Print("[modules] tier0:               {:#x}", tier0);
  logging::console::Print("[modules] particles:           {:#x}", particles);
  logging::console::Print("[modules] resource_system:     {:#x}",
                          resource_system);
  logging::console::Print("[modules] localize:            {:#x}", localize);
  logging::console::Print("[modules] mesh_system:         {:#x}", mesh_system);
  logging::console::Print("[modules] file_system_stdio:   {:#x}",
                          file_system_stdio);
  logging::console::Print("[modules] vphysics2:           {:#x}", vphysics2);

  return client && engine2 && server && scene_system && material_system2 &&
         render_system_dx11 && panorama && schema_system && input_system &&
         sound_system && tier0 && particles && resource_system && localize &&
         mesh_system && file_system_stdio && vphysics2;
}

}  // namespace addresses::modules
