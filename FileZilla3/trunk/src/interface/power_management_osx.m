#include <Foundation/NSProcessInfo.h>

NSProcessInfo * processInfo;

void const* PowerManagmentImpl_SetBusy()
{
	NSActivityOptions opt = NSActivityUserInitiated | NSActivityIdleSystemSleepDisabled;
	id activity = [processInfo beginActivityWithOptions:NSActivityUserInitiated reason:@"Preventing idle sleep during transfers and other operations."];
	return CFBridgingRetain(activity);
}

void PowerManagmentImpl_SetIdle(void* activity)
{
	if (activity) {
		id activityId = CFBridgingRelease(activity);
		[[NSProcessInfo processInfo] endActivity:activityId];
	}
}


