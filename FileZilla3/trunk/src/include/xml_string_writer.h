#ifndef FILEZILLA_XML_STRING_WRITER_HEADER
#define FILEZILLA_XML_STRING_WRITER_HEADER

#include "libfilezilla_engine.h"

#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif

struct xml_string_writer : pugi::xml_writer
{
	std::string result_;

	virtual void write(const void* data, size_t size) override
	{
		result_.append(static_cast<const char*>(data), size);
	}
};

#endif
