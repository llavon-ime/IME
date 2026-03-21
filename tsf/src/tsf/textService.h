#pragma once

#include <msctf.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <functional>
#include <list>
#include <string>

#include "candidateListUI.hpp"
#include "core/bopomofoBuffer.hpp"
#include "utils/debugSink.hpp"

namespace tsf {

class EditSession : public winrt::implements<EditSession, ITfEditSession> {
public:
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (oper) {
            DebugSink::instance().send(L"INFO", L"EditSession DoEditSession called with operation set");
            oper(ec);
        } else {
            DebugSink::instance().send(L"INFO", L"EditSession DoEditSession called with no operation set");
        }
        return S_OK;
    }

    void set_operation(std::function<void(TfEditCookie)> func) {
        this->oper = std::move(func);
    }

private:
    std::function<void(TfEditCookie)> oper;
};

// clang-format off
class TextService : 
    public winrt::implements<
        TextService,
        ITfTextInputProcessor,
        ITfTextInputProcessorEx, 
        ITfThreadMgrEventSink,                                
        ITfKeyEventSink, 
        ITfCompositionSink, 
        ITfDisplayAttributeProvider> {
    // clang-format on
public:
    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus, ITfDocumentMgr* pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pContext) override;
    STDMETHODIMP OnPopContext(ITfContext* pContext) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pContext, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

private:
    HRESULT activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId);
    void deactivate();

    HRESULT start_composition(ITfContext* pContext);
    HRESULT end_composition(ITfContext* pContext);
    HRESULT set_composition_text(ITfContext* pContext, const std::wstring& text);
    bool candidate_ui_is_active() const;
    bool handle_candidate_ui_key(ITfContext* pContext, WPARAM wParam, BOOL* pfEaten);
    void refresh_composition_after_candidate_finalize(ITfContext* pContext);
    void show_candidate_list_for_current_input(ITfContext* pContext, bool expand);
    bool query_candidate_anchor(ITfContext* pContext, POINT* anchor);
    void hide_candidate_list();
    void show_candidate_list(
        ITfContext* pContext, std::variant<Word, CompositionUnit>& pos, const std::vector<std::wstring>& candidates);

    winrt::com_ptr<ITfThreadMgr> threadMgr;
    TfClientId _tfClientId = TF_CLIENTID_NULL;
    DWORD dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    winrt::com_ptr<ITfComposition> itfComposition;
    winrt::com_ptr<CandidateListUIElement> candidateListUIElement = winrt::make_self<CandidateListUIElement>();
    DWORD dwUIElementId = TF_INVALID_COOKIE;
    BopomofoBuffer compositionBuffer;
};

};  // namespace tsf
