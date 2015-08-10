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

class CXmlFile
{
public:
	CXmlFile() {};
	explicit CXmlFile(const wxString& fileName, wxString const& root = wxString());

	virtual ~CXmlFile();

	pugi::xml_node CreateEmpty();

	wxString GetFileName() const { return m_fileName; }
	void SetFileName(const wxString& name);

	bool HasFileName() const { return !m_fileName.empty(); }

	// Sets error description on failure
	pugi::xml_node Load();

	wxString GetError() const { return m_error; }
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
	wxString GetRedirectedName() const;

	// Opens the specified XML file if it exists or creates a new one otherwise.
	// Returns 0 on error.
	bool GetXmlFile(wxString const& file);

	// Sets version and platform in root element
	void UpdateMetadata();

	// Save the XML document to the given file
	bool SaveXmlFile();

	CDateTime m_modificationTime;
	wxString m_fileName;
	pugi::xml_document m_document;
	pugi::xml_node m_element;

	wxString m_error;

	wxString m_rootName{_T("FileZilla3")};
};

// Convert the given utf-8 string into wxString
wxString ConvLocal(const char *value);

void SetTextAttribute(pugi::xml_node node, const char* name, const wxString& value);
wxString GetTextAttribute(pugi::xml_node node, const char* name);

int GetAttributeInt(pugi::xml_node node, const char* name);
void SetAttributeInt(pugi::xml_node node, const char* name, int value);

pugi::xml_node FindElementWithAttribute(pugi::xml_node node, const char* element, const char* attribute, const char* value);

// Add a new child element with the specified name and value to the xml document
pugi::xml_node AddTextElement(pugi::xml_node node, const char* name, const wxString& value, bool overwrite = false);
void AddTextElement(pugi::xml_node node, const char* name, int64_t value, bool overwrite = false);
pugi::xml_node AddTextElementRaw(pugi::xml_node node, const char* name, const char* value, bool overwrite = false);

// Set the current element's text value
void AddTextElement(pugi::xml_node node, const wxString& value);
void AddTextElement(pugi::xml_node node, int64_t value);
void AddTextElementRaw(pugi::xml_node node, const char* value);

// Get string from named child element
wxString GetTextElement(pugi::xml_node node, const char* name);
wxString GetTextElement_Trimmed(pugi::xml_node node, const char* name);

// Get string from current element
wxString GetTextElement(pugi::xml_node node);
wxString GetTextElement_Trimmed(pugi::xml_node node);

// Get (64-bit) integer from named element
int64_t GetTextElementInt(pugi::xml_node node, const char* name, int defValue = 0);

bool GetTextElementBool(pugi::xml_node node, const char* name, bool defValue = false);

// Functions to save and retrieve CServer objects to the XML file
void SetServer(pugi::xml_node node, const CServer& server);
bool GetServer(pugi::xml_node node, CServer& server);

#endif //__XMLFUNCTIONS_H__
