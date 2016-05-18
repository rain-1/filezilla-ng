#ifndef FILEZILLA_FILE_UTILS_HEADER
#define FILEZILLA_FILE_UTILS_HEADER

#include <libfilezilla/recursive_remove.hpp>

bool UnquoteCommand(wxString& command, wxString& arguments, bool is_dde = false);
bool ProgramExists(const wxString& editor);
bool PathExpand(wxString& cmd);

wxString GetSystemOpenCommand(wxString file, bool &program_exists);

// Returns a file:// URL
wxString GetAsURL(wxString const& dir);

// Opens specified directory in local file manager, e.g. Explorer on Windows
bool OpenInFileManager(const wxString& dir);

bool RenameFile(wxWindow* pWnd, wxString dir, wxString from, wxString to);

CLocalPath GetDownloadDir();

class gui_recursive_remove : public fz::recursive_remove
{
public:
	gui_recursive_remove(wxWindow* parent)
		: parent_(parent)
	{}

#ifdef FZ_WINDOWS
	virtual void adjust_shfileop(SHFILEOPSTRUCT & op)
	{
		op.hwnd = parent_ ? (HWND)parent_->GetHandle() : 0;
		if (parent_) {
			// Move to trash if shift is not pressed, else delete
			op.fFlags = wxGetKeyState(WXK_SHIFT) ? 0 : FOF_ALLOWUNDO;
		}
		else {
			recursive_remove::adjust_shfileop(op);
		}
	}
#endif

	virtual bool confirm() const {
		if (parent_) {
			if (wxMessageBoxEx(_("Really delete all selected files and/or directories from your computer?"), _("Confirmation needed"), wxICON_QUESTION | wxYES_NO, parent_) != wxYES)
				return false;
		}
		return true;
	}

private:
	wxWindow* parent_;
};

#endif
