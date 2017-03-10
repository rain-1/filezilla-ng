#ifndef FILEZILLA_ENGINE_LOGGING_HEADER
#define FILEZILLA_ENGINE_LOGGING_HEADER

enum class MessageType
{
	Status,
	Error,
	Command,
	Response,
	Debug_Warning,
	Debug_Info,
	Debug_Verbose,
	Debug_Debug,

	RawList,

	count
};

#endif

