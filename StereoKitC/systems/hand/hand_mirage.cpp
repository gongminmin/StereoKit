#include "../../stereokit.h"
#include "../platform/openxr.h"
#include "input_hand.h"

#if WINDOWS_UWP
#include <chrono>

#include "winrt/Windows.UI.Core.h" 
#include "winrt/Windows.UI.Input.Spatial.h" 
#include "winrt/Windows.Perception.h"
#include "winrt/Windows.Perception.People.h"
#include "winrt/Windows.Perception.Spatial.h"
#include "winrt/Windows.Foundation.Numerics.h"
#include "winrt/Windows.ApplicationModel.Core.h"
#include "winrt/Windows.Foundation.Collections.h"
#include "winrt/Windows.Foundation.h"

using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::People;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation;
#endif

namespace sk {

#if WINDOWS_UWP
SpatialStationaryFrameOfReference xr_spatial_stage       = nullptr;
SpatialInteractionManager         xr_interaction_manager = nullptr;
bool                              xr_hand_support        = false;
hand_joint_t xr_hand_data[2][27] = {};
bool mirage_checked = false;
#endif

///////////////////////////////////////////

bool hand_mirage_available() {
#if WINDOWS_UWP
	if (mirage_checked)
		return xr_hand_support;
	mirage_checked = true;

	CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(CoreDispatcherPriority::Normal, []() {
		xr_spatial_stage       = SpatialLocator::GetDefault().CreateStationaryFrameOfReferenceAtCurrentLocation();
		xr_interaction_manager = SpatialInteractionManager::GetForCurrentView();
		xr_hand_support        = xr_interaction_manager.IsSourceKindSupported(SpatialInteractionSourceKind::Hand);
	});

	return xr_hand_support;
#else
	return false;
#endif
}

///////////////////////////////////////////

void hand_mirage_init(){
#if !WINDOWS_UWP
	log_err("Mirage hands aren't available in this build!");
#endif
}

///////////////////////////////////////////

void hand_mirage_shutdown() {
}

///////////////////////////////////////////

void hand_mirage_update_hands(int64_t win32_prediction_time) {
#if WINDOWS_UWP
	if (!xr_hand_support)
		return;

	// Convert the time we're given into a format that Windows likes
	PerceptionTimestamp stamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(TimeSpan(win32_prediction_time));
	IVectorView<SpatialInteractionSourceState> sources = xr_interaction_manager.GetDetectedSourcesAtTimestamp(stamp);

	for (auto sourceState : sources)
	{
		HandPose pose = sourceState.TryGetHandPose();
		if (!pose || !xr_spatial_stage)
			continue;

		// Grab the joints from windows
		JointPose               poses[27];
		SpatialCoordinateSystem coordinates = xr_spatial_stage.CoordinateSystem();
		handed_                 handed      = sourceState.Source().Handedness() == SpatialInteractionSourceHandedness::Left ? handed_left : handed_right;
		bool gotJoints = pose.TryGetJoints(
			coordinates,
			{   HandJointKind::ThumbMetacarpal,  HandJointKind::ThumbMetacarpal,  HandJointKind::ThumbProximal,      HandJointKind::ThumbDistal,  HandJointKind::ThumbTip,
				HandJointKind::IndexMetacarpal,  HandJointKind::IndexProximal,    HandJointKind::IndexIntermediate,  HandJointKind::IndexDistal,  HandJointKind::IndexTip,
				HandJointKind::MiddleMetacarpal, HandJointKind::MiddleProximal,   HandJointKind::MiddleIntermediate, HandJointKind::MiddleDistal, HandJointKind::MiddleTip,
				HandJointKind::RingMetacarpal,   HandJointKind::RingProximal,     HandJointKind::RingIntermediate,   HandJointKind::RingDistal,   HandJointKind::RingTip,
				HandJointKind::LittleMetacarpal, HandJointKind::LittleProximal,   HandJointKind::LittleIntermediate, HandJointKind::LittleDistal, HandJointKind::LittleTip,
				HandJointKind::Palm, HandJointKind::Wrist, },
			poses);
		
		// Convert the data from their format to ours
		if (gotJoints) {
			SpatialInteractionSourceLocation location = sourceState.Properties().TryGetLocation(coordinates);

			// Take it from Mirage coordinates to the origin
			pose_t hand_to_origin;
			memcpy(&hand_to_origin.position,    &location.Position().Value(),    sizeof(vec3));
			memcpy(&hand_to_origin.orientation, &location.Orientation().Value(), sizeof(quat));
			hand_to_origin.orientation = quat_inverse(hand_to_origin.orientation);

			// Take it from the origin, to our coordinates
			pose_t hand_to_world = {};
			openxr_get_space(xr_input.handSpace[handed], prediction, hand_to_world);
			
			// Aaaand convert!
			for (size_t i = 0; i < 27; i++) {
				memcpy(&xr_hand_data[handed][i].position,    &poses[i].Position,    sizeof(vec3));
				memcpy(&xr_hand_data[handed][i].orientation, &poses[i].Orientation, sizeof(quat));
				xr_hand_data[handed][i].position    = hand_to_origin.orientation * (xr_hand_data[handed][i].position - hand_to_origin.position);
				xr_hand_data[handed][i].position    = (hand_to_world.orientation * xr_hand_data[handed][i].position) + hand_to_world.position; 
				xr_hand_data[handed][i].orientation = xr_hand_data[handed][i].orientation * hand_to_origin.orientation;
				xr_hand_data[handed][i].orientation = xr_hand_data[handed][i].orientation * hand_to_world.orientation;
				xr_hand_data[handed][i].radius = (poses[i].Radius * 0.85f);
			}
			
			// Copy the data into the input system
			hand_t& inp_hand = (hand_t&)input_hand(handed);
			inp_hand.tracked_state = button_state_active;
			hand_joint_t* pose = input_hand_get_pose_buffer(handed);
			memcpy(pose, &xr_hand_data[handed][0], sizeof(hand_joint_t) * 25);

			quat face_forward = quat_from_angles(-90,0,0);
			inp_hand.palm  = pose_t{ xr_hand_data[handed][25].position, face_forward * xr_hand_data[handed][25].orientation };
			inp_hand.wrist = pose_t{ xr_hand_data[handed][26].position, face_forward * quat_from_angles(-90,0,0) * xr_hand_data[handed][26].orientation };
		} else {
			log_warn("Couldn't get hand joints!");
		}
	}
#endif
}

///////////////////////////////////////////

void hand_mirage_update_frame() {
	hand_mirage_update_hands(openxr_get_time());
}

///////////////////////////////////////////

void hand_mirage_update_predicted() {
	hand_mirage_update_hands(openxr_get_time());
	input_hand_draw();
}

}