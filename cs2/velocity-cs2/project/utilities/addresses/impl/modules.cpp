#include <pch/pch.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>

#include "../addresses.hpp"

namespace addresses::modules {

	bool initialize( )
	{
		client              = MODULE_BASE("client.dll");
		engine2             = MODULE_BASE("engine2.dll");
		server              = MODULE_BASE("server.dll");
		scene_system        = MODULE_BASE("scenesystem.dll");
		material_system2    = MODULE_BASE("materialsystem2.dll");
		render_system_dx11  = MODULE_BASE("rendersystemdx11.dll");
		panorama            = MODULE_BASE("panorama.dll");
		game_overlay_renderer = MODULE_BASE("gameoverlayrenderer64.dll");
		schema_system       = MODULE_BASE("schemasystem.dll");
		input_system        = MODULE_BASE("inputsystem.dll");
		sound_system        = MODULE_BASE("soundsystem.dll");
		tier0               = MODULE_BASE("tier0.dll");
		particles           = MODULE_BASE("particles.dll");
		resource_system     = MODULE_BASE("resourcesystem.dll");
		localize            = MODULE_BASE("localize.dll");
		mesh_system         = MODULE_BASE("meshsystem.dll");
		file_system_stdio   = MODULE_BASE("filesystem_stdio.dll");
		vphysics2           = MODULE_BASE("vphysics2.dll");

		logging::console::print( "[modules] client:              {:#x}", client );
		logging::console::print( "[modules] engine2:             {:#x}", engine2 );
		logging::console::print( "[modules] server:              {:#x}", server );
		logging::console::print( "[modules] scene_system:        {:#x}", scene_system );
		logging::console::print( "[modules] material_system2:    {:#x}", material_system2 );
		logging::console::print( "[modules] render_system_dx11:  {:#x}", render_system_dx11 );
		logging::console::print( "[modules] panorama:            {:#x}", panorama );
		logging::console::print( "[modules] game_overlay:        {:#x}", game_overlay_renderer );
		logging::console::print( "[modules] schema_system:       {:#x}", schema_system );
		logging::console::print( "[modules] input_system:        {:#x}", input_system );
		logging::console::print( "[modules] sound_system:        {:#x}", sound_system );
		logging::console::print( "[modules] tier0:               {:#x}", tier0 );
		logging::console::print( "[modules] particles:           {:#x}", particles );
		logging::console::print( "[modules] resource_system:     {:#x}", resource_system );
		logging::console::print( "[modules] localize:            {:#x}", localize );
		logging::console::print( "[modules] mesh_system:         {:#x}", mesh_system );
		logging::console::print( "[modules] file_system_stdio:   {:#x}", file_system_stdio );
		logging::console::print( "[modules] vphysics2:           {:#x}", vphysics2 );

		return client && engine2 && server && scene_system && material_system2 && render_system_dx11 && panorama && schema_system && input_system && sound_system && tier0 && particles && resource_system && localize && mesh_system && file_system_stdio && vphysics2;
	}

} // namespace addresses::modules
