/*
 * xmlfunctions.h declares some useful xml helper functions, especially to
 * improve usability together with wxWidgets.
 */

#ifndef __XMLFUNCTIONS_H__
#define __XMLFUNCTIONS_H__

#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif

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

void SetTextAttribute(pugi::xml_node node, char const* name, std::string const& value);
void SetTextAttribute(pugi::xml_node node, char const* name, std::wstring const& value);
void SetTextAttributeUtf8(pugi::xml_node node, char const* name, std::string const& utf8);
std::wstring GetTextAttribute(pugi::xml_node node, char const* name);

int GetAttributeInt(pugi::xml_node node, char const* name);
void SetAttributeInt(pugi::xml_node node, char const* name, int value);

pugi::xml_node FindElementWithAttribute(pugi::xml_node node, char const* element, char const* attribute, char const* value);

// Add a new child element with the specified name and value to the xml document
pugi::xml_node AddTextElement(pugi::xml_node node, char const* name, std::string const& value, bool overwrite = false);
pugi::xml_node AddTextElement(pugi::xml_node node, char const* name, std::wstring const& value, bool overwrite = false);
void AddTextElement(pugi::xml_node node, char const* name, int64_t value, bool overwrite = false);
pugi::xml_node AddTextElementUtf8(pugi::xml_node node, char const* name, std::string const& value, bool overwrite = false);

// Set the current element's text value
void AddTextElement(pugi::xml_node node, std::string const& value);
void AddTextElement(pugi::xml_node node, std::wstring const& value);
void AddTextElement(pugi::xml_node node, int64_t value);
void AddTextElementUtf8(pugi::xml_node node, std::string const& value);

// Get string from named child element
std::wstring GetTextElement(pugi::xml_node node, const char* name);
std::wstring GetTextElement_Trimmed(pugi::xml_node node, const char* name);

// Get string from current element
std::wstring GetTextElement(pugi::xml_node node);
std::wstring GetTextElement_Trimmed(pugi::xml_node node);

// Get (64-bit) integer from named element
int64_t GetTextElementInt(pugi::xml_node node, const char* name, int defValue = 0);

bool GetTextElementBool(pugi::xml_node node, const char* name, bool defValue = false);

// Functions to save and retrieve CServer objects to the XML file
void SetServer(pugi::xml_node node, const CServer& server);
bool GetServer(pugi::xml_node node, CServer& server);

#endif //__XMLFUNCTIONS_H__
