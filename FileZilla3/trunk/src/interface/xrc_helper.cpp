#include "filezilla.h"

#include "xrc_helper.h"

#include <wx/xrc/xh_animatctrl.h>
#include <wx/xrc/xh_bmpbt.h>
#include <wx/xrc/xh_bttn.h>
#include <wx/xrc/xh_chckb.h>
#include <wx/xrc/xh_chckl.h>
#include <wx/xrc/xh_choic.h>
#include <wx/xrc/xh_dlg.h>
#include <wx/xrc/xh_gauge.h>
#include <wx/xrc/xh_listb.h>
#include <wx/xrc/xh_listc.h>
#include <wx/xrc/xh_menu.h>
#include <wx/xrc/xh_notbk.h>
#include <wx/xrc/xh_panel.h>
#include <wx/xrc/xh_radbt.h>
#include <wx/xrc/xh_scwin.h>
#include <wx/xrc/xh_sizer.h>
#include <wx/xrc/xh_spin.h>
#include <wx/xrc/xh_stbmp.h>
#include <wx/xrc/xh_stbox.h>
#include <wx/xrc/xh_stlin.h>
#include <wx/xrc/xh_sttxt.h>
#include "xh_text_ex.h"
#include <wx/xrc/xh_tree.h>
#include <wx/xrc/xh_hyperlink.h>

void InitHandlers(wxXmlResource& res)
{
	res.AddHandler(new wxMenuXmlHandler);
	res.AddHandler(new wxMenuBarXmlHandler);
	res.AddHandler(new wxDialogXmlHandler);
	res.AddHandler(new wxPanelXmlHandler);
	res.AddHandler(new wxSizerXmlHandler);
	res.AddHandler(new wxButtonXmlHandler);
	res.AddHandler(new wxBitmapButtonXmlHandler);
	res.AddHandler(new wxStaticTextXmlHandler);
	res.AddHandler(new wxStaticBoxXmlHandler);
	res.AddHandler(new wxStaticBitmapXmlHandler);
	res.AddHandler(new wxTreeCtrlXmlHandler);
	res.AddHandler(new wxListCtrlXmlHandler);
	res.AddHandler(new wxCheckListBoxXmlHandler);
	res.AddHandler(new wxChoiceXmlHandler);
	res.AddHandler(new wxGaugeXmlHandler);
	res.AddHandler(new wxCheckBoxXmlHandler);
	res.AddHandler(new wxSpinCtrlXmlHandler);
	res.AddHandler(new wxRadioButtonXmlHandler);
	res.AddHandler(new wxNotebookXmlHandler);
	res.AddHandler(new wxTextCtrlXmlHandlerEx);
	res.AddHandler(new wxListBoxXmlHandler);
	res.AddHandler(new wxStaticLineXmlHandler);
	res.AddHandler(new wxScrolledWindowXmlHandler);
	res.AddHandler(new wxHyperlinkCtrlXmlHandler);
	res.AddHandler(new wxAnimationCtrlXmlHandler);
	res.AddHandler(new wxStdDialogButtonSizerXmlHandler);
}
