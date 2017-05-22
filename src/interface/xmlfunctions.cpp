#include <filezilla.h>
#include "buildinfo.h"
#include "xmlfunctions.h"
#include "Options.h"
#include <wx/ffile.h>
#include <wx/log.h>

#include <libfilezilla/local_filesys.hpp>

CXmlFile::CXmlFile(std::wstring const& fileName, std::string const& root)
{
	if (!root.empty()) {
		m_rootName = root;
	}
	SetFileName(fileName);
}

void CXmlFile::SetFileName(std::wstring const& name)
{
	assert(!name.empty());
	m_fileName = name;
	m_modificationTime = fz::datetime();
}

pugi::xml_node CXmlFile::Load(bool overwriteInvalid)
{
	Close();
	m_error.clear();

	wxCHECK(!m_fileName.empty(), m_element);

	std::wstring redirectedName = GetRedirectedName();

	GetXmlFile(redirectedName);
	if (!m_element) {
		wxString err = wxString::Format(_("The file '%s' could not be loaded."), m_fileName);
		if (m_error.empty()) {
			err += wxString(_T("\n")) + _("Make sure the file can be accessed and is a well-formed XML document.");
		}
		else {
			err += _T("\n") + m_error;
		}

		// Try the backup file
		GetXmlFile(redirectedName + _T("~"));
		if (!m_element) {
			// Loading backup failed.

			// Create new one if we are allowed to create empty file
			bool createEmpty = overwriteInvalid;

			// Also, if both original and backup file are empty, create new file.
			if (fz::local_filesys::get_size(fz::to_native(redirectedName)) <= 0 && fz::local_filesys::get_size(fz::to_native(redirectedName + _T("~"))) <= 0) {
				createEmpty = true;
			}

			if (createEmpty) {
				m_error.clear();
				CreateEmpty();
				m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
				return m_element;
			}

			// File corrupt and no functional backup, give up.
			m_error = err;
			m_modificationTime.clear();
			return m_element;
		}


		// Loading the backup file succeeded, restore file
		bool res;
		{
			wxLogNull null;
			res = wxCopyFile(redirectedName + _T("~"), redirectedName);
		}
		if (!res) {
			// Could not restore backup, give up.
			Close();
			m_error = err.ToStdWstring();
			m_error += _T("\n") + wxString::Format(_("The valid backup file %s could not be restored"), redirectedName + _T("~")).ToStdWstring();
			m_modificationTime.clear();
			return m_element;
		}

		// We no longer need the backup
		wxRemoveFile(redirectedName + _T("~"));
		m_error.clear();
	}

	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
	return m_element;
}

bool CXmlFile::Modified()
{
	wxCHECK(!m_fileName.empty(), false);

	if (m_modificationTime.empty())
		return true;

	fz::datetime const modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));
	if (!modificationTime.empty() && modificationTime == m_modificationTime)
		return false;

	return true;
}

void CXmlFile::Close()
{
	m_element = pugi::xml_node();
	m_document.reset();
}

void CXmlFile::UpdateMetadata()
{
	if (!m_element || std::string(m_element.name()) != "FileZilla3") {
		return;
	}

	SetTextAttribute(m_element, "version", CBuildInfo::GetVersion());

	std::string const platform =
#ifdef FZ_WINDOWS
		"windows";
#elif defined(FZ_MAC)
		"mac";
#else
		"*nix";
#endif
	SetTextAttributeUtf8(m_element, "platform", platform);
}

bool CXmlFile::Save(bool printError)
{
	m_error.clear();

	wxCHECK(!m_fileName.empty(), false);
	wxCHECK(m_document, false);

	UpdateMetadata();

	bool res = SaveXmlFile();
	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));

	if (!res && printError) {
		assert(!m_error.empty());

		wxString msg = wxString::Format(_("Could not write \"%s\":"), m_fileName);
		wxMessageBoxEx(msg + _T("\n") + m_error, _("Error writing xml file"), wxICON_ERROR);
	}
	return res;
}

pugi::xml_node CXmlFile::CreateEmpty()
{
	Close();

	pugi::xml_node decl = m_document.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";
	decl.append_attribute("encoding") = "UTF-8";

	m_element = m_document.append_child(m_rootName.c_str());
	return m_element;
}

pugi::xml_node AddTextElement(pugi::xml_node node, const char* name, std::string const& value, bool overwrite)
{
	return AddTextElementUtf8(node, name, fz::to_utf8(value), overwrite);
}

pugi::xml_node AddTextElement(pugi::xml_node node, const char* name, std::wstring const& value, bool overwrite)
{
	return AddTextElementUtf8(node, name, fz::to_utf8(value), overwrite);
}

void AddTextElement(pugi::xml_node node, const char* name, int64_t value, bool overwrite)
{
	if (overwrite) {
		node.remove_child(name);
	}
	auto child = node.append_child(name);
	child.text().set(static_cast<long long>(value));
}

pugi::xml_node AddTextElementUtf8(pugi::xml_node node, const char* name, std::string const& value, bool overwrite)
{
	assert(node);

	if (overwrite) {
		node.remove_child(name);
	}

	auto element = node.append_child(name);
	if (!value.empty()) {
		element.text().set(value.c_str());
	}

	return element;
}

void AddTextElement(pugi::xml_node node, std::string const& value)
{
	AddTextElementUtf8(node, fz::to_utf8(value));
}

void AddTextElement(pugi::xml_node node, std::wstring const& value)
{
	AddTextElementUtf8(node, fz::to_utf8(value));
}

void AddTextElement(pugi::xml_node node, int64_t value)
{
	assert(node);
	node.text().set(static_cast<long long>(value));
}

void AddTextElementUtf8(pugi::xml_node node, std::string const& value)
{
	assert(node);
	node.text().set(value.c_str());
}

std::wstring GetTextElement_Trimmed(pugi::xml_node node, const char* name)
{
	return fz::trimmed(GetTextElement(node, name));
}

std::wstring GetTextElement(pugi::xml_node node, const char* name)
{
	wxASSERT(node);

	return fz::to_wstring_from_utf8(node.child_value(name));
}

std::wstring GetTextElement_Trimmed(pugi::xml_node node)
{
	return fz::trimmed(GetTextElement(node));
}

std::wstring GetTextElement(pugi::xml_node node)
{
	wxASSERT(node);

	return fz::to_wstring_from_utf8(node.child_value());
}

int64_t GetTextElementInt(pugi::xml_node node, const char* name, int defValue /*=0*/)
{
	wxASSERT(node);
	return static_cast<int64_t>(node.child(name).text().as_llong(defValue));
}

bool GetTextElementBool(pugi::xml_node node, const char* name, bool defValue /*=false*/)
{
	wxASSERT(node);
	return node.child(name).text().as_bool(defValue);
}

// Opens the specified XML file if it exists or creates a new one otherwise.
// Returns false on error.
bool CXmlFile::GetXmlFile(std::wstring const& file)
{
	Close();

	if (fz::local_filesys::get_size(fz::to_native(file)) <= 0) {
		return false;
	}

	// File exists, open it
	auto result = m_document.load_file(static_cast<wchar_t const*>(file.c_str()));
	if (!result) {
		m_error += fz::sprintf(_T("%s at offset %d."), result.description(), result.offset);
		return false;
	}

	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		if (m_document.first_child()) { // Beware: parse_declaration and parse_doctype can break this
			// Not created by FileZilla3
			Close();
			m_error = _("Unknown root element, the file does not appear to be generated by FileZilla.").ToStdWstring();
			return false;
		}
		m_element = m_document.append_child(m_rootName.c_str());
	}

	return true;
}

std::wstring CXmlFile::GetRedirectedName() const
{
	std::wstring redirectedName = m_fileName;
	bool isLink = false;
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, 0) == fz::local_filesys::file) {
		if (isLink) {
			CLocalPath target(fz::to_wstring(fz::local_filesys::get_link_target(fz::to_native(redirectedName))));
			if (!target.empty()) {
				redirectedName = target.GetPath();
				redirectedName.pop_back();
			}
		}
	}
	return redirectedName;
}

bool CXmlFile::SaveXmlFile()
{
	bool exists = false;

	bool isLink = false;
	int flags = 0;

	wxString redirectedName = GetRedirectedName();
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, &flags) == fz::local_filesys::file) {
#ifdef __WXMSW__
		if (flags & FILE_ATTRIBUTE_HIDDEN)
			SetFileAttributes(redirectedName.c_str(), flags & ~FILE_ATTRIBUTE_HIDDEN);
#endif

		exists = true;
		bool res;
		{
			wxLogNull null;
			res = wxCopyFile(redirectedName, redirectedName + _T("~"));
		}
		if (!res) {
			m_error = _("Failed to create backup copy of xml file").ToStdWstring();
			return false;
		}
	}

	bool success = m_document.save_file(static_cast<wchar_t const*>(redirectedName.c_str()));

	if (!success) {
		wxRemoveFile(redirectedName);
		if (exists) {
			wxLogNull null;
			wxRenameFile(redirectedName + _T("~"), redirectedName);
		}
		m_error = _("Failed to write xml file").ToStdWstring();
		return false;
	}

	if (exists)
		wxRemoveFile(redirectedName + _T("~"));

	return true;
}

bool GetServer(pugi::xml_node node, ServerWithCredentials & server)
{
	wxASSERT(node);

	std::wstring host = GetTextElement(node, "Host");
	if (host.empty()) {
		return false;
	}

	int port = GetTextElementInt(node, "Port");
	if (port < 1 || port > 65535) {
		return false;
	}

	if (!server.server.SetHost(host, port)) {
		return false;
	}

	int const protocol = GetTextElementInt(node, "Protocol");
	if (protocol < 0 || protocol > ServerProtocol::MAX_VALUE) {
		return false;
	}
	server.server.SetProtocol(static_cast<ServerProtocol>(protocol));

	int type = GetTextElementInt(node, "Type");
	if (type < 0 || type >= SERVERTYPE_MAX) {
		return false;
	}

	server.server.SetType(static_cast<ServerType>(type));

	int logonType = GetTextElementInt(node, "Logontype");
	if (logonType < 0 || logonType >= static_cast<int>(LogonType::count)) {
		return false;
	}

	server.SetLogonType(static_cast<LogonType>(logonType));
	
	if (server.credentials.logonType_ != LogonType::anonymous) {
		std::wstring user = GetTextElement(node, "User");
		if (user.empty() && server.credentials.logonType_ != LogonType::interactive && server.credentials.logonType_ != LogonType::ask) {
			return false;
		}

		std::wstring pass, key;
		if (server.credentials.logonType_ == LogonType::normal || server.credentials.logonType_ == LogonType::account) {
			auto passElement = node.child("Pass");
			if (passElement) {
				std::wstring encoding = GetTextAttribute(passElement, "encoding");

				if (encoding == _T("base64")) {
					std::string decoded = fz::base64_decode(passElement.child_value());
					pass = fz::to_wstring_from_utf8(decoded);
				}
				else if (encoding == _T("crypt")) {
					pass = fz::to_wstring_from_utf8(passElement.child_value());
					server.credentials.encrypted_ = public_key::from_base64(passElement.attribute("pubkey").value());
					if (!server.credentials.encrypted_) {
						pass.clear();
						server.SetLogonType(LogonType::ask);
					}
				}
				else if (!encoding.empty()) {
					server.SetLogonType(LogonType::ask);
				}
				else {
					pass = GetTextElement(passElement);
				}
			}
		}
		else if (server.credentials.logonType_ == LogonType::key) {
			key = GetTextElement(node, "Keyfile");

			// password should be empty if we're using a key file
			pass.clear();

			server.credentials.keyFile_ = key;
		}

		server.SetUser(user);
		server.credentials.SetPass(pass);

		server.credentials.account_ = GetTextElement(node, "Account");
	}

	int timezoneOffset = GetTextElementInt(node, "TimezoneOffset");
	if (!server.server.SetTimezoneOffset(timezoneOffset)) {
		return false;
	}

	wxString pasvMode = GetTextElement(node, "PasvMode");
	if (pasvMode == _T("MODE_PASSIVE")) {
		server.server.SetPasvMode(MODE_PASSIVE);
	}
	else if (pasvMode == _T("MODE_ACTIVE")) {
		server.server.SetPasvMode(MODE_ACTIVE);
	}
	else {
		server.server.SetPasvMode(MODE_DEFAULT);
	}

	int maximumMultipleConnections = GetTextElementInt(node, "MaximumMultipleConnections");
	server.server.MaximumMultipleConnections(maximumMultipleConnections);

	wxString encodingType = GetTextElement(node, "EncodingType");
	if (encodingType == _T("Auto")) {
		server.server.SetEncodingType(ENCODING_AUTO);
	}
	else if (encodingType == _T("UTF-8")) {
		server.server.SetEncodingType(ENCODING_UTF8);
	}
	else if (encodingType == _T("Custom")) {
		std::wstring customEncoding = GetTextElement(node, "CustomEncoding");
		if (customEncoding.empty()) {
			return false;
		}
		if (!server.server.SetEncodingType(ENCODING_CUSTOM, customEncoding)) {
			return false;
		}
	}
	else {
		server.server.SetEncodingType(ENCODING_AUTO);
	}

	if (CServer::SupportsPostLoginCommands(server.server.GetProtocol())) {
		std::vector<std::wstring> postLoginCommands;
		auto element = node.child("PostLoginCommands");
		if (element) {
			for (auto commandElement = element.child("Command"); commandElement; commandElement = commandElement.next_sibling("Command")) {
				std::wstring command = fz::to_wstring_from_utf8(commandElement.child_value());
				if (!command.empty()) {
					postLoginCommands.emplace_back(std::move(command));
				}
			}
		}
		if (!server.server.SetPostLoginCommands(postLoginCommands)) {
			return false;
		}
	}

	server.server.SetBypassProxy(GetTextElementInt(node, "BypassProxy", false) == 1);
	server.server.SetName(GetTextElement_Trimmed(node, "Name"));

	if (server.server.GetName().empty()) {
		server.server.SetName(GetTextElement_Trimmed(node));
	}

	return true;
}

void SetServer(pugi::xml_node node, ServerWithCredentials const& server)
{
	if (!node) {
		return;
	}

	bool kiosk_mode = COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0;

	for (auto child = node.first_child(); child; child = node.first_child()) {
		node.remove_child(child);
	}

	AddTextElement(node, "Host", server.server.GetHost());
	AddTextElement(node, "Port", server.server.GetPort());
	AddTextElement(node, "Protocol", server.server.GetProtocol());
	AddTextElement(node, "Type", server.server.GetType());

	ProtectedCredentials credentials = server.credentials;

	if (credentials.logonType_ != LogonType::anonymous) {
		AddTextElement(node, "User", server.server.GetUser());

		credentials.Protect();

		if (credentials.logonType_ == LogonType::normal || credentials.logonType_ == LogonType::account) {
			std::string pass = fz::to_utf8(credentials.GetPass());

			if (credentials.encrypted_) {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", pass);
				if (passElement) {
					SetTextAttribute(passElement, "encoding", _T("crypt"));
					SetTextAttributeUtf8(passElement, "pubkey", credentials.encrypted_.to_base64());
				}
			}
			else {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", fz::base64_encode(pass));
				if (passElement) {
					SetTextAttribute(passElement, "encoding", _T("base64"));
				}
			}

			if (credentials.logonType_ == LogonType::account) {
				AddTextElement(node, "Account", credentials.account_);
			}
		}
		else if (!credentials.keyFile_.empty()) {
			AddTextElement(node, "Keyfile", credentials.keyFile_);
		}
	}
	AddTextElement(node, "Logontype", static_cast<int>(credentials.logonType_));

	AddTextElement(node, "TimezoneOffset", server.server.GetTimezoneOffset());
	switch (server.server.GetPasvMode())
	{
	case MODE_PASSIVE:
		AddTextElementUtf8(node, "PasvMode", "MODE_PASSIVE");
		break;
	case MODE_ACTIVE:
		AddTextElementUtf8(node, "PasvMode", "MODE_ACTIVE");
		break;
	default:
		AddTextElementUtf8(node, "PasvMode", "MODE_DEFAULT");
		break;
	}
	AddTextElement(node, "MaximumMultipleConnections", server.server.MaximumMultipleConnections());

	switch (server.server.GetEncodingType())
	{
	case ENCODING_AUTO:
		AddTextElementUtf8(node, "EncodingType", "Auto");
		break;
	case ENCODING_UTF8:
		AddTextElementUtf8(node, "EncodingType", "UTF-8");
		break;
	case ENCODING_CUSTOM:
		AddTextElementUtf8(node, "EncodingType", "Custom");
		AddTextElement(node, "CustomEncoding", server.server.GetCustomEncoding());
		break;
	}

	if (CServer::SupportsPostLoginCommands(server.server.GetProtocol())) {
		std::vector<std::wstring> const& postLoginCommands = server.server.GetPostLoginCommands();
		if (!postLoginCommands.empty()) {
			auto element = node.append_child("PostLoginCommands");
			for (auto const& command : postLoginCommands) {
				AddTextElement(element, "Command", command);
			}
		}
	}

	AddTextElementUtf8(node, "BypassProxy", server.server.GetBypassProxy() ? "1" : "0");
	std::wstring const& name = server.server.GetName();
	if (!name.empty()) {
		AddTextElement(node, "Name", name);
	}
}

void SetTextAttribute(pugi::xml_node node, char const* name, std::string const& value)
{
	SetTextAttributeUtf8(node, name, fz::to_utf8(value));
}

void SetTextAttribute(pugi::xml_node node, char const* name, std::wstring const& value)
{
	SetTextAttributeUtf8(node, name, fz::to_utf8(value));
}

void SetTextAttributeUtf8(pugi::xml_node node, char const* name, std::string const& utf8)
{
	assert(node);
	auto attribute = node.attribute(name);
	if (!attribute) {
		attribute = node.append_attribute(name);
	}
	attribute.set_value(utf8.c_str());
}

std::wstring GetTextAttribute(pugi::xml_node node, char const* name)
{
	assert(node);

	const char* value = node.attribute(name).value();
	return fz::to_wstring_from_utf8(value);
}

pugi::xml_node FindElementWithAttribute(pugi::xml_node node, const char* element, const char* attribute, const char* value)
{
	pugi::xml_node child = element ? node.child(element) : node.first_child();
	while (child) {
		const char* nodeVal = child.attribute(attribute).value();
		if (nodeVal && !strcmp(value, nodeVal)) {
			return child;
		}

		child = element ? child.next_sibling(element) : child.next_sibling();
	}

	return child;
}

int GetAttributeInt(pugi::xml_node node, const char* name)
{
	return node.attribute(name).as_int();
}

void SetAttributeInt(pugi::xml_node node, const char* name, int value)
{
	auto attribute = node.attribute(name);
	if (!attribute) {
		attribute = node.append_attribute(name);
	}
	attribute.set_value(value);
}

namespace {
struct xml_memory_writer : pugi::xml_writer
{
	size_t written{};
	char* buffer{};
	size_t remaining{};

	virtual void write(const void* data, size_t size)
	{
		if (buffer && size <= remaining) {
			memcpy(buffer, data, size);
			buffer += size;
			remaining -= size;
		}
		written += size;
	}
};
}

size_t CXmlFile::GetRawDataLength()
{
	if (!m_document) {
		return 0;
	}

	xml_memory_writer writer;
	m_document.save(writer);
	return writer.written;
}

void CXmlFile::GetRawDataHere(char* p, size_t size) // p has to big enough to hold at least GetRawDataLength() bytes
{
	if (size) {
		memset(p, 0, size);
	}
	xml_memory_writer writer;
	writer.buffer = p;
	writer.remaining = size;
	m_document.save(writer);
}

bool CXmlFile::ParseData(char* data)
{
	Close();
	m_document.load_string(data);
	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		Close();
	}
	return !!m_element;
}

bool CXmlFile::IsFromFutureVersion() const
{
	if (!m_element) {
		return false;
	}
	std::wstring const version = GetTextAttribute(m_element, "version");
	return CBuildInfo::ConvertToVersionNumber(CBuildInfo::GetVersion().c_str()) < CBuildInfo::ConvertToVersionNumber(version.c_str());
}
