#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/gauge.h>
#include <wx/textdlg.h>
#include <wx/statline.h>
#include <wx/mstream.h>
#include <thread>
#include "resource.h"
#include "RyujinCore.hh"

class RyujinApp : public wxApp {

private:
    wxTextCtrl* m_input = nullptr;
    wxTextCtrl* m_pdb = nullptr;
    wxTextCtrl* m_output = nullptr;
    wxCheckBox* m_virtualize = nullptr;
    wxCheckBox* m_junk = nullptr;
    wxCheckBox* m_encrypt = nullptr;
    wxCheckBox* m_randomSection = nullptr;
    wxCheckBox* m_obfuscateIat = nullptr;
    wxCheckBox* m_ignoreOriginalCodeRemove = nullptr;
    wxCheckBox* m_isAntiDebugWithTroll = nullptr;
    wxCheckBox* m_isAntiDebugNormal = nullptr;
    wxCheckBox* m_isAntiDump = nullptr;
    wxListBox* m_procList = nullptr;
    wxGauge* m_progress = nullptr;

    auto DrawnPathRow(wxWindow* parent, wxBoxSizer* sizer, const wxString& label, int buttonId) -> wxTextCtrl*;
    auto DrawnStyledCheckbox(wxWindow* parent, const wxString& label) -> wxCheckBox*;
    auto DrawnRyujinButton(wxWindow* parent, const wxString& label, int id) -> wxButton*;
    auto BindFileDialogs(wxFrame* frame) -> void;
    auto BindListEvents(wxFrame* frame) -> void;
    auto BindRunEvent(wxFrame* frame) -> void;
    auto LoadPngFromRes(wxWindow* parent, wxWindowID id, int resId) -> wxStaticBitmap*;

public:
    bool OnInit() override;

};

wxIMPLEMENT_APP(RyujinApp);
