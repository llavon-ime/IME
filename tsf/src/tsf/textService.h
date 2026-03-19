#pragma once

#include <msctf.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <functional>
#include <list>
#include <string>

#include "utils/debugSink.hpp"

namespace tsf {

// class EditSession : public winrt::implements<EditSession, ITfEditSession> {
// public:
//     STDMETHODIMP DoEditSession(TfEditCookie ec) override {
//         DebugSink::instance().send(
//             L"INFO", L"EditSession DoEditSession called : operations count = " + std::to_wstring(operations.size()));
//         for (auto& oper : operations) {
//             oper(ec);
//         }
//         return S_OK;
//     }

//     int operations_count() const {
//         return static_cast<int>(operations.size());
//     }

//     void add_operation(std::function<void(TfEditCookie)> oper) {
//         operations.push_back(std::move(oper));
//     }

// private:
//     std::list<std::function<void(TfEditCookie)>> operations;
// };

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
    TextService();
    ~TextService();

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

    winrt::com_ptr<ITfThreadMgr> threadMgr;
    TfClientId _tfClientId = TF_CLIENTID_NULL;
    DWORD dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    winrt::com_ptr<ITfComposition> itfComposition;
    std::wstring compositionBuffer;
};

};  // namespace tsf
