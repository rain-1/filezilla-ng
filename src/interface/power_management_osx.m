#include <Foundation/NSProcessInfo.h>

void* PowerManagmentImpl_SetBusy()
{
	id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated reason:@"Preventing idle sleep during transfers and other operations."];
	return activity;
}

void PowerManagmentImpl_SetIdle(void* activity)
{
	[[NSProcessInfo processInfo] endActivity:activity];
}


