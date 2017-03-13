#include <Foundation/NSProcessInfo.h>

void const* PowerManagmentImpl_SetBusy()
{
	NSActivityOptions opt = NSActivityUserInitiated | NSActivityIdleSystemSleepDisabled;
	id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:opt reason:@"Preventing idle sleep during transfers and other operations."];
	return CFBridgingRetain(activity);
}

void PowerManagmentImpl_SetIdle(void* activity)
{
	if (activity) {
		id<NSObject> activityId = CFBridgingRelease(activity);
		[[NSProcessInfo processInfo] endActivity:activityId];
	}
}


