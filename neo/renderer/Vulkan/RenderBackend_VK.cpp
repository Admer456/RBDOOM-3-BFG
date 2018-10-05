/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013-2015 Robert Beckebans
Copyright (C) 2016-2017 Dustin Land

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "precompiled.h"

#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../../framework/Common_local.h"

idCVar r_drawFlickerBox( "r_drawFlickerBox", "0", CVAR_RENDERER | CVAR_BOOL, "visual test for dropping frames" );
idCVar stereoRender_warp( "stereoRender_warp", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use the optical warping renderprog instead of stereoDeGhost" );

idCVar r_showSwapBuffers( "r_showSwapBuffers", "0", CVAR_BOOL, "Show timings from GL_BlockingSwapBuffers" );
idCVar r_syncEveryFrame( "r_syncEveryFrame", "1", CVAR_BOOL, "Don't let the GPU buffer execution past swapbuffers" );


// NEW VULKAN STUFF

idCVar r_vkEnableValidationLayers( "r_vkEnableValidationLayers", "0", CVAR_BOOL, "" );

vulkanContext_t vkcontext;

#define ID_VK_ERROR_STRING( x ) case x: return #x

/*
=============
VK_ErrorToString
=============
*/
const char* VK_ErrorToString( VkResult result )
{
	switch( result )
	{
			ID_VK_ERROR_STRING( VK_SUCCESS );
			ID_VK_ERROR_STRING( VK_NOT_READY );
			ID_VK_ERROR_STRING( VK_TIMEOUT );
			ID_VK_ERROR_STRING( VK_EVENT_SET );
			ID_VK_ERROR_STRING( VK_EVENT_RESET );
			ID_VK_ERROR_STRING( VK_INCOMPLETE );
			ID_VK_ERROR_STRING( VK_ERROR_OUT_OF_HOST_MEMORY );
			ID_VK_ERROR_STRING( VK_ERROR_OUT_OF_DEVICE_MEMORY );
			ID_VK_ERROR_STRING( VK_ERROR_INITIALIZATION_FAILED );
			ID_VK_ERROR_STRING( VK_ERROR_DEVICE_LOST );
			ID_VK_ERROR_STRING( VK_ERROR_MEMORY_MAP_FAILED );
			ID_VK_ERROR_STRING( VK_ERROR_LAYER_NOT_PRESENT );
			ID_VK_ERROR_STRING( VK_ERROR_EXTENSION_NOT_PRESENT );
			ID_VK_ERROR_STRING( VK_ERROR_FEATURE_NOT_PRESENT );
			ID_VK_ERROR_STRING( VK_ERROR_INCOMPATIBLE_DRIVER );
			ID_VK_ERROR_STRING( VK_ERROR_TOO_MANY_OBJECTS );
			ID_VK_ERROR_STRING( VK_ERROR_FORMAT_NOT_SUPPORTED );
			ID_VK_ERROR_STRING( VK_ERROR_SURFACE_LOST_KHR );
			ID_VK_ERROR_STRING( VK_ERROR_NATIVE_WINDOW_IN_USE_KHR );
			ID_VK_ERROR_STRING( VK_SUBOPTIMAL_KHR );
			ID_VK_ERROR_STRING( VK_ERROR_OUT_OF_DATE_KHR );
			ID_VK_ERROR_STRING( VK_ERROR_INCOMPATIBLE_DISPLAY_KHR );
			ID_VK_ERROR_STRING( VK_ERROR_VALIDATION_FAILED_EXT );
			ID_VK_ERROR_STRING( VK_ERROR_INVALID_SHADER_NV );
			ID_VK_ERROR_STRING( VK_RESULT_BEGIN_RANGE );
			ID_VK_ERROR_STRING( VK_RESULT_RANGE_SIZE );
		default:
			return "UNKNOWN";
	};
}

static const int g_numInstanceExtensions = 2;
static const char* g_instanceExtensions[ g_numInstanceExtensions ] =
{
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};

static const int g_numDebugInstanceExtensions = 1;
static const char* g_debugInstanceExtensions[ g_numDebugInstanceExtensions ] =
{
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME
};

static const int g_numDeviceExtensions = 1;
static const char* g_deviceExtensions[ g_numDeviceExtensions ] =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static const int g_numValidationLayers = 1;
static const char* g_validationLayers[ g_numValidationLayers ] =
{
	"VK_LAYER_LUNARG_standard_validation"
};


/*
=========================================================================================================

DEBUGGING AND VALIDATION

=========================================================================================================
*/

/*
=============
DebugCallback
=============
*/
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback( VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject,
		size_t location, int32_t msgCode, const char* layerPrefix, const char* msg,
		void* userData )
{
	if( msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT )
	{
		idLib::Printf( "[Vulkan] ERROR: [ %s ] Code %d : '%s'\n", layerPrefix, msgCode, msg );
	}
	else if( msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT )
	{
		idLib::Printf( "[Vulkan] WARNING: [ %s ] Code %d : '%s'\n", layerPrefix, msgCode, msg );
	}
	else if( msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT )
	{
		idLib::Printf( "[Vulkan] PERFORMANCE WARNING: [ %s ] Code %d : '%s'\n", layerPrefix, msgCode, msg );
	}
	else if( msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT )
	{
		idLib::Printf( "[Vulkan] INFO: [ %s ] Code %d : '%s'\n", layerPrefix, msgCode, msg );
	}
	else if( msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT )
	{
		idLib::Printf( "[Vulkan] DEBUG: [ %s ] Code %d : '%s'\n", layerPrefix, msgCode, msg );
	}
	
	/*
	 * false indicates that layer should not bail-out of an
	 * API call that had validation failures. This may mean that the
	 * app dies inside the driver due to invalid parameter(s).
	 * That's what would happen without validation layers, so we'll
	 * keep that behavior here.
	 */
	return VK_FALSE;
}

/*
=============
CreateDebugReportCallback
=============
*/
static void CreateDebugReportCallback()
{
#if 1
	vk::DebugReportCallbackCreateInfoEXT callbackInfo;
	
	callbackInfo.setFlags( vk::DebugReportFlagBitsEXT::eDebug | vk::DebugReportFlagBitsEXT::eWarning | vk::DebugReportFlagBitsEXT::eError ); // VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT
	callbackInfo.setPfnCallback( DebugCallback );
	
	PFN_vkCreateDebugReportCallbackEXT func = ( PFN_vkCreateDebugReportCallbackEXT ) vkcontext.instance.getProcAddr( "vkCreateDebugReportCallbackEXT" );
	ID_VK_VALIDATE( func != NULL, "Could not find vkCreateDebugReportCallbackEXT" );
	
	//kDebugReportCallbackEXT callback = (VkDebugReportCallbackEXT)vkcontext.callback;
	ID_VKPP_CHECK( vkcontext.instance.createDebugReportCallbackEXT( &callbackInfo, NULL, &vkcontext.callback, vkcontext.dispatcher ) );
#else
	VkDebugReportCallbackCreateInfoEXT callbackInfo = {};
	callbackInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
	callbackInfo.flags = VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
	callbackInfo.pfnCallback = ( PFN_vkDebugReportCallbackEXT ) DebugCallback;
	
	PFN_vkCreateDebugReportCallbackEXT func = ( PFN_vkCreateDebugReportCallbackEXT ) vkGetInstanceProcAddr( VkInstance( vkcontext.instance ), "vkCreateDebugReportCallbackEXT" );
	ID_VK_VALIDATE( func != NULL, "Could not find vkCreateDebugReportCallbackEXT" );
	
	VkDebugReportCallbackEXT callback = ( VkDebugReportCallbackEXT )vkcontext.callback;
	ID_VK_CHECK( func( VkInstance( vkcontext.instance ), &callbackInfo, NULL, &callback ) );
#endif
}

/*
=============
DestroyDebugReportCallback
=============
*/
static void DestroyDebugReportCallback()
{
#if 1
	PFN_vkDestroyDebugReportCallbackEXT func = ( PFN_vkDestroyDebugReportCallbackEXT ) vkcontext.instance.getProcAddr( "vkDestroyDebugReportCallbackEXT" );
	ID_VK_VALIDATE( func != NULL, "Could not find vkDestroyDebugReportCallbackEXT" );
	vkcontext.instance.destroyDebugReportCallbackEXT( vkcontext.callback, NULL, vkcontext.dispatcher );
#else
	PFN_vkDestroyDebugReportCallbackEXT func = ( PFN_vkDestroyDebugReportCallbackEXT ) vkGetInstanceProcAddr( VkInstance( vkcontext.instance ), "vkDestroyDebugReportCallbackEXT" );
	ID_VK_VALIDATE( func != NULL, "Could not find vkDestroyDebugReportCallbackEXT" );
	func( VkInstance( vkcontext.instance ), VkDebugReportCallbackEXT( vkcontext.callback ), NULL );
#endif
}

/*
=============
ValidateValidationLayers
=============
*/
static void ValidateValidationLayers()
{
	uint32 instanceLayerCount = 0;
	vk::enumerateInstanceLayerProperties( &instanceLayerCount, NULL );
	
	idList< vk::LayerProperties > instanceLayers;
	instanceLayers.SetNum( instanceLayerCount );
	vk::enumerateInstanceLayerProperties( &instanceLayerCount, instanceLayers.Ptr() );
	
	bool found = false;
	for( uint32 i = 0; i < g_numValidationLayers; ++i )
	{
		for( uint32 j = 0; j < instanceLayerCount; ++j )
		{
			if( i == 0 )
			{
				idLib::Printf( "Found Vulkan Validation Layer '%s'\n", instanceLayers[j].layerName );
			}
			
			if( idStr::Icmp( g_validationLayers[i], instanceLayers[j].layerName ) == 0 )
			{
				found = true;
				break;
			}
		}
		
		if( !found )
		{
			idLib::FatalError( "Cannot find validation layer: %s.\n", g_validationLayers[ i ] );
		}
	}
}
/*
=============
CreateVulkanInstance
=============
*/
static void CreateVulkanInstance()
{
	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = GAME_NAME;
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = "idTech 4.5x";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_MAKE_VERSION( 1, 0, VK_HEADER_VERSION );
	
	vk::InstanceCreateInfo createInfo;
	createInfo.pApplicationInfo = &appInfo;
	
	const bool enableLayers = r_vkEnableValidationLayers.GetBool();
	
	vkcontext.instanceExtensions.Clear();
	vkcontext.deviceExtensions.Clear();
	vkcontext.validationLayers.Clear();
	
	for( int i = 0; i < g_numInstanceExtensions; ++i )
	{
		vkcontext.instanceExtensions.Append( g_instanceExtensions[ i ] );
	}
	
	for( int i = 0; i < g_numDeviceExtensions; ++i )
	{
		vkcontext.deviceExtensions.Append( g_deviceExtensions[ i ] );
	}
	
	if( enableLayers )
	{
		for( int i = 0; i < g_numDebugInstanceExtensions; ++i )
		{
			vkcontext.instanceExtensions.Append( g_debugInstanceExtensions[ i ] );
		}
		
		for( int i = 0; i < g_numValidationLayers; ++i )
		{
			vkcontext.validationLayers.Append( g_validationLayers[ i ] );
		}
		
		ValidateValidationLayers();
	}
	
	createInfo.enabledExtensionCount = vkcontext.instanceExtensions.Num();
	createInfo.ppEnabledExtensionNames = vkcontext.instanceExtensions.Ptr();
	createInfo.enabledLayerCount = vkcontext.validationLayers.Num();
	createInfo.ppEnabledLayerNames = vkcontext.validationLayers.Ptr();
	
	ID_VKPP_CHECK( vk::createInstance( &createInfo, NULL, &vkcontext.instance ) );
	
	// RB: fetch all function pointers through the passed instance
	vkcontext.dispatcher.init( vkcontext.instance );
	
	if( enableLayers )
	{
		CreateDebugReportCallback();
	}
}

/*
=============
CreateSurface
=============
*/
#ifdef _WIN32
#include "../../sys/win32/win_local.h"
#endif

static void CreateSurface()
{
#ifdef _WIN32
	vk::Win32SurfaceCreateInfoKHR createInfo;
	createInfo.hinstance = win32.hInstance;
	createInfo.hwnd = win32.hWnd;
	
	ID_VKPP_CHECK( vkcontext.instance.createWin32SurfaceKHR( &createInfo, NULL, &vkcontext.surface ) );
	
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	VkWaylandSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.display = info.display;
	createInfo.surface = info.window;
	
	ID_VK_CHECK( vkCreateWaylandSurfaceKHR( info.inst, &createInfo, NULL, &info.surface ) );
	
#else
	VkXcbSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.connection = info.connection;
	createInfo.window = info.window;
	
	ID_VK_CHECK( vkCreateXcbSurfaceKHR( info.inst, &createInfo, NULL, &info.surface ) );
#endif  // _WIN32
	
	
}

/*
=============
EnumeratePhysicalDevices
=============
*/
static void EnumeratePhysicalDevices()
{
	uint32 numDevices = 0;
	ID_VKPP_CHECK( vkcontext.instance.enumeratePhysicalDevices( &numDevices, NULL ) );
	ID_VK_VALIDATE( numDevices > 0, "vkEnumeratePhysicalDevices returned zero devices." );
	
	idList< vk::PhysicalDevice > devices;
	devices.SetNum( numDevices );
	
	ID_VKPP_CHECK( vkcontext.instance.enumeratePhysicalDevices( &numDevices, devices.Ptr() ) );
	ID_VK_VALIDATE( numDevices > 0, "vkEnumeratePhysicalDevices returned zero devices." );
	
	vkcontext.gpus.SetNum( numDevices );
	
	for( uint32 i = 0; i < numDevices; ++i )
	{
		gpuInfo_t& gpu = vkcontext.gpus[ i ];
		gpu.device = devices[ i ];
		
		// get Queue family properties
		{
			uint32 numQueues = 0;
			gpu.device.getQueueFamilyProperties( &numQueues, NULL );
			ID_VK_VALIDATE( numQueues > 0, "vkGetPhysicalDeviceQueueFamilyProperties returned zero queues." );
			
			gpu.queueFamilyProps.SetNum( numQueues );
			gpu.device.getQueueFamilyProperties( &numQueues, gpu.queueFamilyProps.Ptr() );
			ID_VK_VALIDATE( numQueues > 0, "vkGetPhysicalDeviceQueueFamilyProperties returned zero queues." );
		}
		
		// grab available Vulkan extensions
		{
			uint32 numExtension;
			ID_VKPP_CHECK( gpu.device.enumerateDeviceExtensionProperties( NULL, &numExtension, NULL ) );
			ID_VK_VALIDATE( numExtension > 0, "vkEnumerateDeviceExtensionProperties returned zero extensions." );
			
			gpu.extensionProps.SetNum( numExtension );
			ID_VKPP_CHECK( gpu.device.enumerateDeviceExtensionProperties( NULL, &numExtension, gpu.extensionProps.Ptr() ) );
			ID_VK_VALIDATE( numExtension > 0, "vkEnumerateDeviceExtensionProperties returned zero extensions." );
			
#if 0
			for( uint32 j = 0; j < numExtension; j++ )
			{
				idLib::Printf( "Found Vulkan Extension '%s' on device %d\n", gpu.extensionProps[j].extensionName, i );
			}
#endif
		}
		
		// grap surface specific information
		ID_VKPP_CHECK( gpu.device.getSurfaceCapabilitiesKHR( vkcontext.surface, &gpu.surfaceCaps ) );
		
		{
			uint32 numFormats;
			ID_VKPP_CHECK( gpu.device.getSurfaceFormatsKHR( vkcontext.surface, &numFormats, NULL ) );
			ID_VK_VALIDATE( numFormats > 0, "vkGetPhysicalDeviceSurfaceFormatsKHR returned zero surface formats." );
			
			gpu.surfaceFormats.SetNum( numFormats );
			ID_VKPP_CHECK( gpu.device.getSurfaceFormatsKHR( vkcontext.surface, &numFormats, gpu.surfaceFormats.Ptr() ) );
			ID_VK_VALIDATE( numFormats > 0, "vkGetPhysicalDeviceSurfaceFormatsKHR returned zero surface formats." );
		}
		
		{
			uint32 numPresentModes;
			ID_VKPP_CHECK( gpu.device.getSurfacePresentModesKHR( vkcontext.surface, &numPresentModes, NULL ) );
			ID_VK_VALIDATE( numPresentModes > 0, "vkGetPhysicalDeviceSurfacePresentModesKHR returned zero present modes." );
			
			gpu.presentModes.SetNum( numPresentModes );
			ID_VKPP_CHECK( gpu.device.getSurfacePresentModesKHR( vkcontext.surface, &numPresentModes, gpu.presentModes.Ptr() ) );
			ID_VK_VALIDATE( numPresentModes > 0, "vkGetPhysicalDeviceSurfacePresentModesKHR returned zero present modes." );
		}
		
		gpu.device.getMemoryProperties( &gpu.memProps );
		gpu.device.getProperties( &gpu.props );
	}
}

/*
=============
ClearContext
=============
*/
static void ClearContext()
{
	vkcontext.counter = 0;
	vkcontext.currentFrameData = 0;
	vkcontext.jointCacheHandle = 0;
	memset( vkcontext.stencilOperations, 0, sizeof( vkcontext.stencilOperations ) );
	vkcontext.instance = VK_NULL_HANDLE;
	vkcontext.physicalDevice = VK_NULL_HANDLE;
	vkcontext.device = VK_NULL_HANDLE;
	vkcontext.graphicsQueue = VK_NULL_HANDLE;
	vkcontext.presentQueue = VK_NULL_HANDLE;
	vkcontext.graphicsFamilyIdx = -1;
	vkcontext.presentFamilyIdx = -1;
	vkcontext.callback = VK_NULL_HANDLE;
	vkcontext.instanceExtensions.Clear();
	vkcontext.deviceExtensions.Clear();
	vkcontext.validationLayers.Clear();
	vkcontext.gpu = NULL;
	vkcontext.gpus.Clear();
	vkcontext.commandPool = VK_NULL_HANDLE;
	vkcontext.commandBuffer.Zero();
	vkcontext.commandBufferFences.Zero();
	vkcontext.commandBufferRecorded.Zero();
	vkcontext.surface = VK_NULL_HANDLE;
	vkcontext.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	vkcontext.depthFormat = VK_FORMAT_UNDEFINED;
	vkcontext.renderPass = VK_NULL_HANDLE;
	vkcontext.pipelineCache = VK_NULL_HANDLE;
	vkcontext.sampleCount = VK_SAMPLE_COUNT_1_BIT;
	vkcontext.supersampling = false;
	vkcontext.fullscreen = 0;
	vkcontext.swapchain = VK_NULL_HANDLE;
	vkcontext.swapchainFormat = VK_FORMAT_UNDEFINED;
	vkcontext.currentSwapIndex = 0;
	vkcontext.msaaImage = VK_NULL_HANDLE;
	vkcontext.msaaImageView = VK_NULL_HANDLE;
	vkcontext.swapchainImages.Zero();
	vkcontext.frameBuffers.Zero();
	vkcontext.acquireSemaphores.Zero();
	vkcontext.renderCompleteSemaphores.Zero();
	vkcontext.currentImageParm = 0;
	vkcontext.imageParms.Zero();
}

/*
=============
idRenderBackend::idRenderBackend
=============
*/
idRenderBackend::idRenderBackend()
{
	ClearContext();
}

/*
=============
idRenderBackend::~idRenderBackend
=============
*/
idRenderBackend::~idRenderBackend()
{

}

/*
=============================
R_IsInitialized
=============================
*/
static bool r_initialized = false;
bool R_IsInitialized()
{
	return r_initialized;
}

/*
=============
idRenderBackend::Init
=============
*/
void idRenderBackend::Init()
{
	if( R_IsInitialized() )
	{
		idLib::FatalError( "R_InitVulkan called while active" );
	}
	
	// DG: make sure SDL has setup video so getting supported modes in R_SetNewMode() works
	GLimp_PreInit();
	// DG end
	
	R_SetNewMode( true );
	
	// input and sound systems need to be tied to the new window
	Sys_InitInput();
	
	idLib::Printf( "----- Initializing Vulkan driver -----\n" );
	
	// create the Vulkan instance and enable validation layers
	CreateVulkanInstance();
	
	// create the windowing interface
#ifdef _WIN32
	CreateSurface();
#endif
	
	// grab detailed information of available GPUs
	EnumeratePhysicalDevices();
}

/*
=============
idRenderBackend::Shutdown
=============
*/
void idRenderBackend::Shutdown()
{
	// destroy Debug Callback
	if( r_vkEnableValidationLayers.GetBool() )
	{
		DestroyDebugReportCallback();
	}
	
	// destroy the Instance
	vkcontext.instance.destroy( NULL );
	
	// destroy main window
	GLimp_Shutdown();
	r_initialized = false;
}




bool GL_CheckErrors_( const char* filename, int line )
{
	return false;
}

/*
=============
idRenderBackend::DrawElementsWithCounters
=============
*/
void idRenderBackend::DrawElementsWithCounters( const drawSurf_t* surf )
{

	// RB: added stats
	pc.c_drawElements++;
	pc.c_drawIndexes += surf->numIndexes;
}


/*
=========================================================================================================

GL COMMANDS

=========================================================================================================
*/

/*
==================
idRenderBackend::GL_StartFrame
==================
*/
void idRenderBackend::GL_StartFrame()
{

}

/*
==================
idRenderBackend::GL_EndFrame
==================
*/
void idRenderBackend::GL_EndFrame()
{

}

/*
========================
GL_SetDefaultState

This should initialize all GL state that any part of the entire program
may touch, including the editor.
========================
*/
void idRenderBackend::GL_SetDefaultState()
{
	RENDERLOG_PRINTF( "--- GL_SetDefaultState ---\n" );
	
	glStateBits = 0;
	
	hdrAverageLuminance = 0;
	hdrMaxLuminance = 0;
	hdrTime = 0;
	hdrKey = 0;
	
	GL_State( 0, true );
	
	// RB begin
	Framebuffer::Unbind();
	// RB end
	
	
	
	// RB: don't keep renderprogs that were enabled during level load
	renderProgManager.Unbind();
}

/*
====================
idRenderBackend::GL_State

This routine is responsible for setting the most commonly changed state
====================
*/
void idRenderBackend::GL_State( uint64 stateBits, bool forceGlState )
{
	uint64 diff = stateBits ^ glStateBits;
	
	if( !r_useStateCaching.GetBool() || forceGlState )
	{
		// make sure everything is set all the time, so we
		// can see if our delta checking is screwing up
		diff = 0xFFFFFFFFFFFFFFFF;
	}
	else if( diff == 0 )
	{
		return;
	}
	
	// TODO
	
	glStateBits = stateBits;
}

/*
====================
idRenderBackend::SelectTexture
====================
*/
void idRenderBackend::GL_SelectTexture( int index )
{
	if( vkcontext.currentImageParm == index )
	{
		return;
	}
	
	RENDERLOG_PRINTF( "GL_SelectTexture( %d );\n", index );
	
	vkcontext.currentImageParm = index;
}

/*
====================
idRenderBackend::GL_Cull

This handles the flipping needed when the view being
rendered is a mirored view.
====================
*/
void idRenderBackend::GL_Cull( cullType_t cullType )
{
	// TODO REMOVE
}

/*
====================
idRenderBackend::GL_Scissor
====================
*/
void idRenderBackend::GL_Scissor( int x /* left*/, int y /* bottom */, int w, int h )
{
	// TODO
}

/*
====================
idRenderBackend::GL_Viewport
====================
*/
void idRenderBackend::GL_Viewport( int x /* left */, int y /* bottom */, int w, int h )
{
	// TODO
}

/*
====================
idRenderBackend::GL_PolygonOffset
====================
*/
void idRenderBackend::GL_PolygonOffset( float scale, float bias )
{
	// TODO
}

/*
========================
idRenderBackend::GL_DepthBoundsTest
========================
*/
void idRenderBackend::GL_DepthBoundsTest( const float zmin, const float zmax )
{
	// TODO
}

/*
====================
idRenderBackend::GL_Color
====================
*/
void idRenderBackend::GL_Color( float r, float g, float b, float a )
{
	float parm[4];
	parm[0] = idMath::ClampFloat( 0.0f, 1.0f, r );
	parm[1] = idMath::ClampFloat( 0.0f, 1.0f, g );
	parm[2] = idMath::ClampFloat( 0.0f, 1.0f, b );
	parm[3] = idMath::ClampFloat( 0.0f, 1.0f, a );
	renderProgManager.SetRenderParm( RENDERPARM_COLOR, parm );
}

/*
========================
idRenderBackend::GL_Clear
========================
*/
void idRenderBackend::GL_Clear( bool color, bool depth, bool stencil, byte stencilValue, float r, float g, float b, float a, bool clearHDR )
{
	// TODO
	
	/*
	int clearFlags = 0;
	if( color )
	{
		glClearColor( r, g, b, a );
		clearFlags |= GL_COLOR_BUFFER_BIT;
	}
	if( depth )
	{
		clearFlags |= GL_DEPTH_BUFFER_BIT;
	}
	if( stencil )
	{
		glClearStencil( stencilValue );
		clearFlags |= GL_STENCIL_BUFFER_BIT;
	}
	glClear( clearFlags );
	
	// RB begin
	if( r_useHDR.GetBool() && clearHDR && globalFramebuffers.hdrFBO != NULL )
	{
		bool isDefaultFramebufferActive = Framebuffer::IsDefaultFramebufferActive();
	
		globalFramebuffers.hdrFBO->Bind();
		glClear( clearFlags );
	
		if( isDefaultFramebufferActive )
		{
			Framebuffer::Unbind();
		}
	}
	*/
}


/*
=================
idRenderBackend::GL_GetCurrentState
=================
*/
uint64 idRenderBackend::GL_GetCurrentState() const
{
	return glStateBits;
}

/*
========================
idRenderBackend::GL_GetCurrentStateMinusStencil
========================
*/
uint64 idRenderBackend::GL_GetCurrentStateMinusStencil() const
{
	return GL_GetCurrentState() & ~( GLS_STENCIL_OP_BITS | GLS_STENCIL_FUNC_BITS | GLS_STENCIL_FUNC_REF_BITS | GLS_STENCIL_FUNC_MASK_BITS );
}


/*
=============
idRenderBackend::CheckCVars

See if some cvars that we watch have changed
=============
*/
void idRenderBackend::CheckCVars()
{
	// TODO
	
	/*
	// gamma stuff
	if( r_gamma.IsModified() || r_brightness.IsModified() )
	{
		r_gamma.ClearModified();
		r_brightness.ClearModified();
		R_SetColorMappings();
	}
	
	// filtering
	if( r_maxAnisotropicFiltering.IsModified() || r_useTrilinearFiltering.IsModified() || r_lodBias.IsModified() )
	{
		idLib::Printf( "Updating texture filter parameters.\n" );
		r_maxAnisotropicFiltering.ClearModified();
		r_useTrilinearFiltering.ClearModified();
		r_lodBias.ClearModified();
	
		for( int i = 0 ; i < globalImages->images.Num() ; i++ )
		{
			if( globalImages->images[i] )
			{
				globalImages->images[i]->Bind();
				globalImages->images[i]->SetTexParameters();
			}
		}
	}
	
	extern idCVar r_useSeamlessCubeMap;
	if( r_useSeamlessCubeMap.IsModified() )
	{
		r_useSeamlessCubeMap.ClearModified();
		if( glConfig.seamlessCubeMapAvailable )
		{
			if( r_useSeamlessCubeMap.GetBool() )
			{
				glEnable( GL_TEXTURE_CUBE_MAP_SEAMLESS );
			}
			else
			{
				glDisable( GL_TEXTURE_CUBE_MAP_SEAMLESS );
			}
		}
	}
	
	extern idCVar r_useSRGB;
	if( r_useSRGB.IsModified() )
	{
		r_useSRGB.ClearModified();
		if( glConfig.sRGBFramebufferAvailable )
		{
			if( r_useSRGB.GetBool() && r_useSRGB.GetInteger() != 3 )
			{
				glEnable( GL_FRAMEBUFFER_SRGB );
			}
			else
			{
				glDisable( GL_FRAMEBUFFER_SRGB );
			}
		}
	}
	
	if( r_antiAliasing.IsModified() )
	{
		switch( r_antiAliasing.GetInteger() )
		{
			case ANTI_ALIASING_MSAA_2X:
			case ANTI_ALIASING_MSAA_4X:
			case ANTI_ALIASING_MSAA_8X:
				if( r_antiAliasing.GetInteger() > 0 )
				{
					glEnable( GL_MULTISAMPLE );
				}
				break;
	
			default:
				glDisable( GL_MULTISAMPLE );
				break;
		}
	}
	
	if( r_useHDR.IsModified() || r_useHalfLambertLighting.IsModified() )
	{
		r_useHDR.ClearModified();
		r_useHalfLambertLighting.ClearModified();
		renderProgManager.KillAllShaders();
		renderProgManager.LoadAllShaders();
	}
	*/
}

/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

/*
=============
idRenderBackend::DrawFlickerBox
=============
*/
void idRenderBackend::DrawFlickerBox()
{
	if( !r_drawFlickerBox.GetBool() )
	{
		return;
	}
	
	// TODO
	
	/*
	if( tr.frameCount & 1 )
	{
		glClearColor( 1, 0, 0, 1 );
	}
	else
	{
		glClearColor( 0, 1, 0, 1 );
	}
	glScissor( 0, 0, 256, 256 );
	glClear( GL_COLOR_BUFFER_BIT );
	*/
}

/*
=============
idRenderBackend::SetBuffer
=============
*/
void idRenderBackend::SetBuffer( const void* data )
{
	// see which draw buffer we want to render the frame to
	
	const setBufferCommand_t* cmd = ( const setBufferCommand_t* )data;
	
	RENDERLOG_PRINTF( "---------- RB_SetBuffer ---------- to buffer # %d\n", cmd->buffer );
	
	GL_Scissor( 0, 0, tr.GetWidth(), tr.GetHeight() );
	
	// clear screen for debugging
	// automatically enable this with several other debug tools
	// that might leave unrendered portions of the screen
	if( r_clear.GetFloat() || idStr::Length( r_clear.GetString() ) != 1 || r_singleArea.GetBool() || r_showOverDraw.GetBool() )
	{
		float c[3];
		if( sscanf( r_clear.GetString(), "%f %f %f", &c[0], &c[1], &c[2] ) == 3 )
		{
			GL_Clear( true, false, false, 0, c[0], c[1], c[2], 1.0f, true );
		}
		else if( r_clear.GetInteger() == 2 )
		{
			GL_Clear( true, false, false, 0, 0.0f, 0.0f,  0.0f, 1.0f, true );
		}
		else if( r_showOverDraw.GetBool() )
		{
			GL_Clear( true, false, false, 0, 1.0f, 1.0f, 1.0f, 1.0f, true );
		}
		else
		{
			GL_Clear( true, false, false, 0, 0.4f, 0.0f, 0.25f, 1.0f, true );
		}
	}
}

/*
=============
GL_BlockingSwapBuffers

We want to exit this with the GPU idle, right at vsync
=============
*/
void idRenderBackend::BlockingSwapBuffers()
{
	RENDERLOG_PRINTF( "***************** BlockingSwapBuffers *****************\n\n\n" );
	
	if( vkcontext.commandBufferRecorded[ vkcontext.currentFrameData ] == false )
	{
		return;
	}
	
	ID_VK_CHECK( vkWaitForFences( vkcontext.device, 1, &vkcontext.commandBufferFences[ vkcontext.currentFrameData ], VK_TRUE, UINT64_MAX ) );
	
	ID_VK_CHECK( vkResetFences( vkcontext.device, 1, &vkcontext.commandBufferFences[ vkcontext.currentFrameData ] ) );
	vkcontext.commandBufferRecorded[ vkcontext.currentFrameData ] = false;
	
	VkSemaphore* finished = &vkcontext.renderCompleteSemaphores[ vkcontext.currentFrameData ];
	
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = finished;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &vkcontext.swapchain;
	presentInfo.pImageIndices = &vkcontext.currentSwapIndex;
	
	ID_VK_CHECK( vkQueuePresentKHR( vkcontext.presentQueue, &presentInfo ) );
	
	vkcontext.counter++;
	vkcontext.currentFrameData = vkcontext.counter % NUM_FRAME_DATA;
}



/*
====================
idRenderBackend::StereoRenderExecuteBackEndCommands

Renders the draw list twice, with slight modifications for left eye / right eye
====================
*/
void idRenderBackend::StereoRenderExecuteBackEndCommands( const emptyCommand_t* const allCmds )
{
	// RB: TODO ?
}

/*
====================
RB_ExecuteBackEndCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
void idRenderBackend::ExecuteBackEndCommands( const emptyCommand_t* cmds )
{
	// r_debugRenderToTexture
	int c_draw3d = 0;
	int c_draw2d = 0;
	int c_setBuffers = 0;
	int c_copyRenders = 0;
	
	resolutionScale.SetCurrentGPUFrameTime( commonLocal.GetRendererGPUMicroseconds() );
	
	renderLog.StartFrame();
	
	if( cmds->commandId == RC_NOP && !cmds->next )
	{
		return;
	}
	
	if( renderSystem->GetStereo3DMode() != STEREO3D_OFF )
	{
		StereoRenderExecuteBackEndCommands( cmds );
		renderLog.EndFrame();
		return;
	}
	
	uint64 backEndStartTime = Sys_Microseconds();
	
	// needed for editor rendering
	GL_SetDefaultState();
	
	for( ; cmds != NULL; cmds = ( const emptyCommand_t* )cmds->next )
	{
		switch( cmds->commandId )
		{
			case RC_NOP:
				break;
			case RC_DRAW_VIEW_3D:
			case RC_DRAW_VIEW_GUI:
				DrawView( cmds, 0 );
				if( ( ( const drawSurfsCommand_t* )cmds )->viewDef->viewEntitys )
				{
					c_draw3d++;
				}
				else
				{
					c_draw2d++;
				}
				break;
			case RC_SET_BUFFER:
				//RB_SetBuffer( cmds );
				c_setBuffers++;
				break;
			case RC_COPY_RENDER:
				CopyRender( cmds );
				c_copyRenders++;
				break;
			case RC_POST_PROCESS:
				PostProcess( cmds );
				break;
			default:
				common->Error( "RB_ExecuteBackEndCommands: bad commandId" );
				break;
		}
	}
	
	DrawFlickerBox();
	
	// stop rendering on this thread
	uint64 backEndFinishTime = Sys_Microseconds();
	pc.totalMicroSec = backEndFinishTime - backEndStartTime;
	
	if( r_debugRenderToTexture.GetInteger() == 1 )
	{
		common->Printf( "3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, pc.c_copyFrameBuffer );
		pc.c_copyFrameBuffer = 0;
	}
	
	renderLog.EndFrame();
}


/*
==============================================================================================

STENCIL SHADOW RENDERING

==============================================================================================
*/

/*
=====================
idRenderBackend::DrawStencilShadowPass
=====================
*/
void idRenderBackend::DrawStencilShadowPass( const drawSurf_t* drawSurf, const bool renderZPass )
{
}


/*
==============================================================================================

OFFSCREEN RENDERING

==============================================================================================
*/

void Framebuffer::Init()
{
	// TODO
}

void Framebuffer::Shutdown()
{
	// TODO
}

bool Framebuffer::IsDefaultFramebufferActive()
{
	// TODO
	return true;
}

void Framebuffer::Bind()
{
	// TODO
}

void Framebuffer::Unbind()
{
	// TODO
}

bool Framebuffer::IsBound()
{
	// TODO
	return true;
}

void Framebuffer::Check()
{
	// TODO
}

void Framebuffer::CheckFramebuffers()
{
	// TODO
}