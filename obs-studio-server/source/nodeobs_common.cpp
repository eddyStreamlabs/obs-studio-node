#include <iomanip>
#include <map>
#include "nodeobs_content.h"

/* For sceneitem transform modifications.
 * We should consider moving this to another module */
#include <graphics/matrix4.h>

#include "error.hpp"
#include "shared.hpp"

#include <thread>

std::map<std::string, OBS::Display*> displays;
std::string                          sourceSelected;
bool                                 firstDisplayCreation = true;

std::thread* windowMessage = NULL;

/* A lot of the sceneitem functionality is a lazy copy-pasta from the Qt UI. */
// https://github.com/jp9000/obs-studio/blob/master/UI/window-basic-main.cpp#L4888
static void GetItemBox(obs_sceneitem_t* item, vec3& tl, vec3& br)
{
	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3_set(&tl, M_INFINITE, M_INFINITE, 0.0f);
	vec3_set(&br, -M_INFINITE, -M_INFINITE, 0.0f);

	auto GetMinPos = [&](float x, float y) {
		vec3 pos;
		vec3_set(&pos, x, y, 0.0f);
		vec3_transform(&pos, &pos, &boxTransform);
		vec3_min(&tl, &tl, &pos);
		vec3_max(&br, &br, &pos);
	};

	GetMinPos(0.0f, 0.0f);
	GetMinPos(1.0f, 0.0f);
	GetMinPos(0.0f, 1.0f);
	GetMinPos(1.0f, 1.0f);
}

static vec3 GetItemTL(obs_sceneitem_t* item)
{
	vec3 tl, br;
	GetItemBox(item, tl, br);
	return tl;
}

static void SetItemTL(obs_sceneitem_t* item, const vec3& tl)
{
	vec3 newTL;
	vec2 pos;

	obs_sceneitem_get_pos(item, &pos);
	newTL = GetItemTL(item);
	pos.x += tl.x - newTL.x;
	pos.y += tl.y - newTL.y;
	obs_sceneitem_set_pos(item, &pos);
}

static bool CenterAlignSelectedItems(obs_scene_t* scene, obs_sceneitem_t* item, void* param)
{
	obs_bounds_type boundsType = *reinterpret_cast<obs_bounds_type*>(param);

	if (!obs_sceneitem_selected(item))
		return true;

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	obs_transform_info itemInfo;
	vec2_set(&itemInfo.pos, 0.0f, 0.0f);
	vec2_set(&itemInfo.scale, 1.0f, 1.0f);
	itemInfo.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	itemInfo.rot       = 0.0f;

	vec2_set(&itemInfo.bounds, float(ovi.base_width), float(ovi.base_height));
	itemInfo.bounds_type      = boundsType;
	itemInfo.bounds_alignment = OBS_ALIGN_CENTER;

	obs_sceneitem_set_info(item, &itemInfo);

	UNUSED_PARAMETER(scene);
	return true;
}

static bool MultiplySelectedItemScale(obs_scene_t* scene, obs_sceneitem_t* item, void* param)
{
	vec2& mul = *reinterpret_cast<vec2*>(param);

	if (!obs_sceneitem_selected(item))
		return true;

	vec3 tl = GetItemTL(item);

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	vec2_mul(&scale, &scale, &mul);
	obs_sceneitem_set_scale(item, &scale);

	SetItemTL(item, tl);

	UNUSED_PARAMETER(scene);
	return true;
}

void OBS_content::Register(ipc::server& srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Display");

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_createDisplay",
	    std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String},
	    OBS_content_createDisplay));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_destroyDisplay", std::vector<ipc::type>{ipc::type::String}, OBS_content_destroyDisplay));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_getDisplayPreviewOffset",
	    std::vector<ipc::type>{ipc::type::String},
	    OBS_content_getDisplayPreviewOffset));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_getDisplayPreviewSize",
	    std::vector<ipc::type>{ipc::type::String},
	    OBS_content_getDisplayPreviewSize));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_createSourcePreviewDisplay",
	    std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String, ipc::type::String},
	    OBS_content_createSourcePreviewDisplay));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_resizeDisplay",
	    std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_resizeDisplay));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_moveDisplay",
	    std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_moveDisplay));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setPaddingSize",
	    std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32},
	    OBS_content_setPaddingSize));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setPaddingColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setPaddingColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setBackgroundColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setBackgroundColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setOutlineColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setOutlineColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setGuidelineColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setGuidelineColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setResizeBoxOuterColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setResizeBoxOuterColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setResizeBoxInnerColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setResizeBoxInnerColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setResizeBoxInnerColor",
	    std::vector<ipc::type>{
	        ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_setResizeBoxInnerColor));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setShouldDrawUI",
	    std::vector<ipc::type>{ipc::type::String, ipc::type::Int32},
	    OBS_content_setShouldDrawUI));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_selectSource",
	    std::vector<ipc::type>{ipc::type::UInt32, ipc::type::UInt32},
	    OBS_content_selectSource));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_selectSources",
	    std::vector<ipc::type>{ipc::type::UInt32, ipc::type::Binary},
	    OBS_content_selectSources));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_dragSelectedSource",
	    std::vector<ipc::type>{ipc::type::Int32, ipc::type::Int32},
	    OBS_content_dragSelectedSource));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_getDrawGuideLines", std::vector<ipc::type>{ipc::type::String}, OBS_content_getDrawGuideLines));

	cls->register_function(std::make_shared<ipc::function>(
	    "OBS_content_setDrawGuideLines",
	    std::vector<ipc::type>{ipc::type::String, ipc::type::Int32},
	    OBS_content_setDrawGuideLines));

	srv.register_collection(cls);
}

void popupAeroDisabledWindow(void)
{
	#ifdef WIN32
	MessageBox(
	    NULL,
	    TEXT("Streamlabs OBS needs Aero enabled to run properly on Windows 7.  "
	         "If you've disabled Aero for performance reasons, "
	         "you may still use the app, but you will need to keep the window maximized.\n\n\n\n\n"
	         "This is a workaround to keep Streamlabs OBS running and not the preferred route. "
	         "We recommend upgrading to Windows 10 or enabling Aero."),
	    TEXT("Aero is disabled"),
	    MB_OK);
	#endif
}

void OBS_content::OBS_content_createDisplay(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	uint64_t windowHandle = args[0].value_union.ui64;
	auto     found        = displays.find(args[1].value_str);

	/* If found, do nothing since it would
	be a memory leak otherwise. */
	if (found != displays.end()) {
		std::cerr << "Duplicate key provided to createDisplay: " << args[1].value_str << std::endl;
		return;
	}

	#ifdef WIN32
	displays.insert_or_assign(args[1].value_str, new OBS::Display(windowHandle));

	if (!IsWindows8OrGreater()) {
		BOOL enabled = FALSE;
		DwmIsCompositionEnabled(&enabled);
		if (!enabled && firstDisplayCreation) {
			windowMessage = new std::thread(popupAeroDisabledWindow);
		}
	}
	firstDisplayCreation = false;
	#endif
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_destroyDisplay(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	auto found = displays.find(args[0].value_str);

	if (found == displays.end()) {
		std::cerr << "Failed to find key for destruction: " << args[0].value_str << std::endl;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Key does not exist."));
		return;
	}

	if (windowMessage != NULL && windowMessage->joinable())
		windowMessage->join();

	delete found->second;
	displays.erase(found);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_createSourcePreviewDisplay(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	uint64_t windowHandle = args[0].value_union.ui64;

	auto found = displays.find(args[2].value_str);

	/* If found, do nothing since it would
	be a memory leak otherwise. */
	if (found != displays.end()) {
		std::cout << "Duplicate key provided to createDisplay!" << std::endl;
		return;
	}
	#ifdef WIN32
	displays.insert_or_assign(args[2].value_str, new OBS::Display(windowHandle, args[1].value_str));
	#endif
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_resizeDisplay(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		std::cout << "Invalid key provided to resizeDisplay: " << args[0].value_str << std::endl;
		return;
	}

	OBS::Display* display = value->second;

	int width  = args[1].value_union.ui32;
	int height = args[2].value_union.ui32;

	display->SetSize(width, height);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_moveDisplay(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		std::cout << "Invalid key provided to moveDisplay: " << args[0].value_str << std::endl;
		return;
	}

	OBS::Display* display = value->second;

	int x = args[1].value_union.ui32;
	int y = args[2].value_union.ui32;

	display->SetPosition(x, y);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_setPaddingSize(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Validate Arguments
	/// Amount
	/*switch (args.size()) {
	case 0:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate,
					"Usage: OBS_content_setPaddingSize(displayKey<string>, size<number>)")
			)
		);
		return;
	case 1:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "Not enough Parameters")
			)
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
			)
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{size} is not a <number>!")
			)
		);
		return;
	}*/

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
			)
		);*/
		return;
	}

	it->second->SetPaddingSize(args[1].value_union.ui32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setPaddingColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate,
					"Usage: OBS_content_setPaddingColor(displayKey<string>, red<number>{0.0, 255.0}, green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}[, alpha<number>{0.0, 1.0}])")
			)
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "Not enough Parameters")
			)
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
			)
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
			)
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
			)
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
			)
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
			)
		);*/
		return;
	}

	it->second->SetPaddingColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setBackgroundColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate,
					"Usage: OBS_content_setBackgroundColor(displayKey<string>, red<number>{0.0, 255.0}, green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}[, alpha<number>{0.0, 1.0}])")
			)
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "Not enough Parameters")
			)
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
			)
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
			)
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
			)
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
			)
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
			v8::Exception::SyntaxError(
				v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
			)
		);*/
		return;
	}

	it->second->SetBackgroundColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setOutlineColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate,
		                                    "Usage: OBS_content_setOutlineColor(displayKey<string>, red<number>{0.0, 255.0}, green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}[, alpha<number>{0.0, 1.0}])")
		      )
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "Not enough Parameters")
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
		      )
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
		      )
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
		      )
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	it->second->SetOutlineColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setGuidelineColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate,
		                                    "Usage: OBS_content_setGuidelineColor(displayKey<string>, red<number>{0.0, 255.0}, green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}[, alpha<number>{0.0, 1.0}])")
		      )
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "Not enough Parameters")
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
		      )
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
		      )
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
		      )
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	it->second->SetGuidelineColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setResizeBoxOuterColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	const char* usage_string =
	    "Usage: OBS_content_setResizeBoxOuterColor"
	    "(displayKey<string>, red<number>{0.0, 255.0}, "
	    "green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}"
	    "[, alpha<number>{0.0, 1.0}])";

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, usage_string)
			)
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "Not enough Parameters")
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
		      )
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
		      )
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
		      )
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	it->second->SetResizeBoxOuterColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setResizeBoxInnerColor(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	union
	{
		uint32_t rgba;
		uint8_t  c[4];
	} color;

	const char* usage_string =
	    "Usage: OBS_content_setResizeBoxInnerColor"
	    "(displayKey<string>, red<number>{0.0, 255.0},"
	    " green<number>{0.0, 255.0}, blue<number>{0.0, 255.0}"
	    "[, alpha<number>{0.0, 1.0}])";

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, usage_string)
			)
		);
		return;
	case 1:
	case 2:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "Not enough Parameters")
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{red} is not a <number>!")
		      )
		);
		return;
	}
	if (args[2]->IsUndefined() && !args[2]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{green} is not a <number>!")
		      )
		);
		return;
	}
	if (args[3]->IsUndefined() && !args[3]->IsNumber()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{blue} is not a <number>!")
		      )
		);
		return;
	}*/

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	it->second->SetResizeBoxInnerColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setShouldDrawUI(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	const char* usage_string =
	    "Usage: OBS_content_setShouldDrawUI"
	    "(displayKey<string>, value<boolean>)";

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, usage_string)
		      )
		);
		return;
	case 1:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "Not enough Parameters")
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}
	if (args[1]->IsUndefined() && !args[1]->IsBoolean()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{value} is not a <boolean>!")
		      )
		);
		return;
	}*/

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	it->second->SetDrawUI((bool)args[1].value_union.i32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_getDisplayPreviewOffset(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		std::cout << "Invalid key provided to moveDisplay: " << args[0].value_str << std::endl;
		return;
	}

	OBS::Display* display = value->second;

	auto offset = display->GetPreviewOffset();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((int32_t)offset.first));
	rval.push_back(ipc::value((int32_t)offset.second));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_getDisplayPreviewSize(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		std::cout << "Invalid key provided to moveDisplay: " << args[0].value_str << std::endl;
		return;
	}

	OBS::Display* display = value->second;

	auto size = display->GetPreviewSize();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((int32_t)size.first));
	rval.push_back(ipc::value((int32_t)size.second));
	AUTO_DEBUG;
}

/* Deprecated */
void OBS_content::OBS_content_selectSource(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	/* Here we assume that channel 0 holds the one and only transition.
	 * We also assume that the active source within that transition is
	 * the scene that we need */
	obs_source_t* transition = obs_get_output_source(0);
	obs_source_t* source     = obs_transition_get_active_source(transition);
	obs_scene_t*  scene      = obs_scene_from_source(source);

	obs_source_release(transition);

	uint32_t x = args[0].value_union.ui32;
	uint32_t y = args[1].value_union.ui32;

	auto function = [](obs_scene_t*, obs_sceneitem_t* item, void* listSceneItems) {
		vector<obs_sceneitem_t*>& items = *reinterpret_cast<vector<obs_sceneitem_t*>*>(listSceneItems);

		items.push_back(item);
		return true;
	};

	vector<obs_sceneitem_t*> listSceneItems;
	obs_scene_enum_items(scene, function, &listSceneItems);

	bool sourceFound = false;

	for (int i = 0; i < listSceneItems.size(); ++i) {
		obs_sceneitem_t* item       = listSceneItems[i];
		obs_source_t*    source     = obs_sceneitem_get_source(item);
		const char*      sourceName = obs_source_get_name(source);

		struct vec2 position;
		obs_sceneitem_get_pos(item, &position);

		int positionX = int(position.x);
		int positionY = int(position.y);

		int width  = obs_source_get_width(source);
		int height = obs_source_get_height(source);

		if (int(x) >= positionX && int(x) <= width + positionX && int(y) >= positionY && int(y) < height + positionY) {
			sourceSelected = sourceName;
			sourceFound    = true;
			break;
		}
	}

	if (!sourceFound) {
		sourceSelected = "";
		cout << "source not found !!!!" << endl;
	}

	obs_source_release(source);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

/* Deprecated */
bool selectItems(obs_scene_t* scene, obs_sceneitem_t* item, void* param)
{
	vector<std::string>& sources = *reinterpret_cast<vector<std::string>*>(param);

	obs_source_t* source = obs_sceneitem_get_source(item);
	std::string   name   = obs_source_get_name(source);

	if (std::find(sources.begin(), sources.end(), name) != sources.end())
		obs_sceneitem_select(item, true);

	else
		obs_sceneitem_select(item, false);
	return true;
}

/* Deprecated */
void OBS_content::OBS_content_selectSources(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	obs_source_t* transition = obs_get_output_source(0);
	obs_source_t* source     = obs_transition_get_active_source(transition);
	obs_scene_t*  scene      = obs_scene_from_source(source);

	obs_source_release(transition);

	uint16_t                 size = args[0].value_union.ui32;
	std::vector<std::string> tabSources;

	{
		for (int i = 0; i < size; i++) {
			tabSources.push_back(args[i + 1].value_str);
		}

		if (scene)
			obs_scene_enum_items(scene, selectItems, &tabSources);
	}

	obs_source_release(source);
	AUTO_DEBUG;
}

void OBS_content::OBS_content_dragSelectedSource(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	int32_t x = args[0].value_union.i32;
	int32_t y = args[1].value_union.i32;

	if (sourceSelected.compare("") == 0)
		return;

	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	obs_source_t* transition = obs_get_output_source(0);
	obs_source_t* source     = obs_transition_get_active_source(transition);
	obs_scene_t*  scene      = obs_scene_from_source(source);

	obs_source_release(transition);

	obs_sceneitem_t* sourceItem = obs_scene_find_source(scene, sourceSelected.c_str());

	struct vec2 position;
	position.x = float(x);
	position.y = float(y);

	obs_sceneitem_set_pos(sourceItem, &position);
	obs_source_release(source);
	AUTO_DEBUG;
}

void OBS_content::OBS_content_getDrawGuideLines(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	const char* usage_string = "Usage: OBS_content_getDrawGuideLines(displayKey<string>)";

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, usage_string)
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}*/

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		return;
	}

	rval.push_back((bool)it->second->GetDrawGuideLines());
	AUTO_DEBUG;
}

void OBS_content::OBS_content_setDrawGuideLines(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	const char* usage_string =
	    "Usage: OBS_content_getDrawGuideLines"
	    "(displayKey<string>, drawGuideLines<boolean>)";

	// Validate Arguments
	/// Amount
	/*switch (args.Length()) {
	case 0:
	case 1:
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, usage_string)
		      )
		);
		return;
	}

	/// Types
	if (args[0]->IsUndefined() && !args[0]->IsString()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not a <string>!")
		      )
		);
		return;
	}

	if (args[1]->IsUndefined() && !args[1]->IsBoolean()) {
		isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{size} is not a <boolean>!")
		      )
		);
		return;
	}*/

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/

		return;
	}

	it->second->SetDrawGuideLines((bool)args[1].value_union.i32);
	AUTO_DEBUG;
}
