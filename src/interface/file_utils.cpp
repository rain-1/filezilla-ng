#include "filezilla.h"

#include "file_utils.h"

wxString GetAsURL(const wxString& dir)
{
	// Cheap URL encode
	wxString encoded;
	const wxWX2MBbuf utf8 = dir.mb_str(wxConvUTF8);

	if (!utf8)
		return wxEmptyString;

	const char* p = utf8;
	while (*p)
	{
		// List of characters that don't need to be escaped taken
		// from the BNF grammar in RFC 1738
		// Again attention seeking Windows wants special treatment...
		const unsigned char c = (unsigned char)*p++;
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '$' ||
			c == '_' ||
			c == '-' ||
			c == '.' ||
			c == '+' ||
			c == '!' ||
			c == '*' ||
#ifndef __WXMSW__
			c == '\'' ||
#endif
			c == '(' ||
			c == ')' ||
			c == ',' ||
			c == '?' ||
			c == ':' ||
			c == '@' ||
			c == '&' ||
			c == '=' ||
			c == '/')
		{
			encoded += (wxChar)c;
		}
#ifdef __WXMSW__
		else if (c == '\\')
			encoded += '/';
#endif
		else
			encoded += wxString::Format(_T("%%%x"), (unsigned int)c);
	}
#ifdef __WXMSW__
	if (encoded.Left(2) == _T("//"))
	{
		// UNC path
		encoded = encoded.Mid(2);
	}
	else
		encoded = _T("/") + encoded;
#endif
	return _T("file://") + encoded;
}

bool OpenInFileManager(const wxString& dir)
{
	bool ret = false;
#ifdef __WXMSW__
	// Unfortunately under Windows, UTF-8 encoded file:// URLs don't work, so use native paths.
	// Unfortunatelier, we cannot use this for UNC paths, have to use file:// here
	// Unfortunateliest, we again have a problem with UTF-8 characters which we cannot fix...
	if (dir.Left(2) != _T("\\\\") && dir != _T("/"))
		ret = wxLaunchDefaultBrowser(dir);
	else
#endif
	{
		wxString url = GetAsURL(dir);
		if (!url.IsEmpty())
			ret = wxLaunchDefaultBrowser(url);
	}


	if (!ret)
		wxBell();

	return ret;
}

wxString GetSystemOpenCommand(wxString file, bool &program_exists)
{
	wxFileName fn(file);

	const wxString& ext = fn.GetExt();
	if (ext == _T(""))
		return _T("");

	for (;;)
	{
		wxFileType* pType = wxTheMimeTypesManager->GetFileTypeFromExtension(ext);
		if (!pType)
			return _T("");

		wxString cmd;
		if (!pType->GetOpenCommand(&cmd, wxFileType::MessageParameters(file)))
		{
			delete pType;
			return _T("");
		}
		delete pType;

		if (cmd.empty())
			return wxEmptyString;

		program_exists = false;

		wxString editor;
		bool is_dde = false;
#ifdef __WXMSW__
		if (cmd.Left(7) == _T("WX_DDE#"))
		{
			// See wxWidget's wxExecute in src/msw/utilsexc.cpp
			// WX_DDE#<command>#DDE_SERVER#DDE_TOPIC#DDE_COMMAND
			editor = cmd.Mid(7);
			int pos = editor.Find('#');
			if (pos < 1)
				return cmd;
			editor = editor.Left(pos);
			is_dde = true;
		}
		else
#endif
		{
			editor = cmd;
		}

		wxString args;
		if (!UnquoteCommand(editor, args, is_dde) || editor.empty())
			return cmd;

		if (!PathExpand(editor))
			return cmd;

		if (ProgramExists(editor))
			program_exists = true;

#ifdef __WXGTK__
		int pos = args.Find(file);
		if (pos != -1 && file.Find(' ') != -1 && file[0] != '\'' && file[0] != '"')
		{
			// Might need to quote filename, wxWidgets doesn't do it
			if ((!pos || (args[pos - 1] != '\'' && args[pos - 1] != '"')) &&
				args[pos + file.Length()] != '\'' && args[pos + file.Length()] != '"')
			{
				// Filename in command arguments isn't quoted. Repeat with quoted filename
				file = _T("\"") + file + _T("\"");
				continue;
			}
		}
#endif
		return cmd;
	}

	return wxEmptyString;
}

bool UnquoteCommand(wxString& command, wxString& arguments, bool is_dde)
{
	arguments = _T("");

	if (command == _T(""))
		return true;

	wxChar inQuotes = 0;
	wxString file;
	for (unsigned int i = 0; i < command.Len(); i++)
	{
		const wxChar& c = command[i];
		if (c == '"' || c == '\'')
		{
			if (!inQuotes)
				inQuotes = c;
			else if (c != inQuotes)
				file += c;
			else if (command[i + 1] == c)
			{
				file += c;
				i++;
			}
			else
				inQuotes = false;
		}
		else if (command[i] == ' ' && !inQuotes)
		{
			arguments = command.Mid(i + 1);
			arguments.Trim(false);
			break;
		}
		else if (is_dde && !inQuotes && (command[i] == ',' || command[i] == '#'))
		{
			arguments = command.Mid(i + 1);
			arguments.Trim(false);
			break;
		}
		else
			file += command[i];
	}
	if (inQuotes)
		return false;

	command = file;

	return true;
}

bool ProgramExists(const wxString& editor)
{
	if (wxFileName::FileExists(editor))
		return true;

#ifdef __WXMAC__
	if (editor.Right(4) == _T(".app") && wxFileName::DirExists(editor))
		return true;
#endif

	return false;
}

bool PathExpand(wxString& cmd)
{
#ifndef __WXMSW__
	if (cmd[0] == '/')
		return true;
#else
	if (cmd[0] == '\\')
		// UNC or root of current working dir, whatever that is
		return true;
	if (cmd.Len() > 2 && cmd[1] == ':')
		// Absolute path
		return true;
#endif

	// Need to search for program in $PATH
	wxString path;
	if (!wxGetEnv(_T("PATH"), &path))
		return false;

	wxString full_cmd;
	bool found = wxFindFileInPath(&full_cmd, path, cmd);
#ifdef __WXMSW__
	if (!found && cmd.Right(4).Lower() != _T(".exe"))
	{
		cmd += _T(".exe");
		found = wxFindFileInPath(&full_cmd, path, cmd);
	}
#endif

	if (!found)
		return false;

	cmd = full_cmd;
	return true;
}

bool RenameFile(wxWindow* parent, wxString dir, wxString from, wxString to)
{
	if (dir.Right(1) != _T("\\") && dir.Right(1) != _T("/"))
		dir += wxFileName::GetPathSeparator();

#ifdef __WXMSW__
	to = to.Left(255);

	if ((to.Find('/') != -1) ||
		(to.Find('\\') != -1) ||
		(to.Find(':') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('"') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBox(_("Filenames may not contain any of the following characters: / \\ : * ? \" < > |"), _("Invalid filename"), wxICON_EXCLAMATION);
		return false;
	}

	SHFILEOPSTRUCT op;
	memset(&op, 0, sizeof(op));

	from = dir + from + _T(" ");
	from.SetChar(from.Length() - 1, '\0');
	op.pFrom = from;
	to = dir + to + _T(" ");
	to.SetChar(to.Length()-1, '\0');
	op.pTo = to;
	op.hwnd = (HWND)parent->GetHandle();
	op.wFunc = FO_RENAME;
	op.fFlags = FOF_ALLOWUNDO;
	return SHFileOperation(&op) == 0;
#else
	if ((to.Find('/') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBox(_("Filenames may not contain any of the following characters: / * ? < > |"), _("Invalid filename"), wxICON_EXCLAMATION);
		return false;
	}

	return wxRename(dir + from, dir + to) == 0;
#endif
}
