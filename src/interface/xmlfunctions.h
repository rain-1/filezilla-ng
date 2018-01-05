/*
 * xmlfunctions.h declares some useful xml helper functions, especially to
 * improve usability together with wxWidgets.
 */

#ifndef FILEZILLA_INTERFACE_XMLFUNCTIONS_HEADER
#define FILEZILLA_INTERFACE_XMLFUNCTIONS_HEADER

#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif

#include "xmlutils.h"
#include "serverdata.h"

class CXmlFile final
{
public:
	CXmlFile() = default;
	explicit CXmlFile(std::wstring const& fileName, std::string const& root = std::string());

	CXmlFile(CXmlFile const&) = delete;
	CXmlFile& operator=(CXmlFile const&) = delete;

	pugi::xml_node CreateEmpty();

	std::wstring GetFileName() const { return m_fileName; }
	void SetFileName(std::wstring const& name);

	bool HasFileName() const { return !m_fileName.empty(); }

	// Sets error description on failure
	pugi::xml_node Load(bool overwriteInvalid = false);

	std::wstring GetError() const { return m_error; }
	size_t GetRawDataLength();
	void GetRawDataHere(char* p, size_t size); // p has to big enough to hold at least GetRawDataLength() bytes

	bool ParseData(char* data); // data has to be 0-terminated

	void Close();

	pugi::xml_node GetElement() { return m_element; }
	pugi::xml_node const GetElement() const { return m_element; }

	bool Modified();

	bool Save(bool printError);

	bool IsFromFutureVersion() const;
protected:
	std::wstring GetRedirectedName() const;

	// Opens the specified XML file if it exists or creates a new one otherwise.
	// Returns 0 on error.
	bool GetXmlFile(std::wstring const& file);

	// Sets version and platform in root element
	void UpdateMetadata();

	// Save the XML document to the given file
	bool SaveXmlFile();

	fz::datetime m_modificationTime;
	std::wstring m_fileName;
	pugi::xml_document m_document;
	pugi::xml_node m_element;

	std::wstring m_error;

	std::string m_rootName{"FileZilla3"};
};

// Functions to save and retrieve CServer objects to the XML file
void SetServer(pugi::xml_node node, ServerWithCredentials const& server);

bool GetServer(pugi::xml_node node, ServerWithCredentials& server);

#endif
