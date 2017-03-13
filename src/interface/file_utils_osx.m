#include <Foundation/NSFileManager.h>

#include <string.h>

char const* GetDownloadDirImpl()
{
	static char const* path = 0;
	if (!path) {
		NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:nil];
		if (url) {
			path = strdup(url.path.UTF8String);
		}
		else {
			path = "";
		}
	}
	return path;
}
