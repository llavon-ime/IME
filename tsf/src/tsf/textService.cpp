#include "textService.h"

#include "candidateUiController.hpp"
#include "core/bopomofo.hpp"
#include "editSession.hpp"
#include "engine/engine.hpp"
#include "system/globals.h"
#include "utils/debugSink.hpp"
#include "utils/healper.hpp"

using namespace std::literals;

namespace {

inline constexpr winrt::guid kCompositionDisplayAttributeGuid = {
    0x82769d2d, 0x9e5d, 0x4ace, {0x97, 0x53, 0x91, 0x3c, 0x0c, 0x7c, 0x4e, 0x38}};

TF_DISPLAYATTRIBUTE make_composition_display_attribute() {
    TF_DISPLAYATTRIBUTE attribute = {};
    attribute.crText.type = TF_CT_NONE;
    attribute.crBk.type = TF_CT_NONE;
    attribute.lsStyle = TF_LS_DOT;
    attribute.fBoldLine = FALSE;
    attribute.crLine.type = TF_CT_SYSCOLOR;
    attribute.crLine.nIndex = COLOR_WINDOWTEXT;
    attribute.bAttr = TF_ATTR_INPUT;
    return attribute;
}

class CompositionDisplayAttributeInfo
    : public winrt::implements<CompositionDisplayAttributeInfo, ITfDisplayAttributeInfo> {
public:
    CompositionDisplayAttributeInfo() : attribute_(make_composition_display_attribute()), default_(attribute_) {}

    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) {
            return E_INVALIDARG;
        }
        *pguid = kCompositionDisplayAttributeGuid;
        return S_OK;
    }

    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (!pbstrDesc) {
            return E_INVALIDARG;
        }
        *pbstrDesc = SysAllocString(L"Composition");
        return (*pbstrDesc != nullptr) ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) {
            return E_INVALIDARG;
        }
        *pda = attribute_;
        return S_OK;
    }

    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) {
            return E_INVALIDARG;
        }
        attribute_ = *pda;
        return S_OK;
    }

    STDMETHODIMP Reset() override {
        attribute_ = default_;
        return S_OK;
    }

private:
    TF_DISPLAYATTRIBUTE attribute_ = {};
    TF_DISPLAYATTRIBUTE default_ = {};
};

class DisplayAttributeEnum : public winrt::implements<DisplayAttributeEnum, IEnumTfDisplayAttributeInfo> {
public:
    explicit DisplayAttributeEnum(winrt::com_ptr<ITfDisplayAttributeInfo> info, ULONG index = 0)
        : info_(std::move(info)), index_(index) {}

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override {
        if (!ppEnum) {
            return E_INVALIDARG;
        }
        *ppEnum = nullptr;
        auto enumerator = winrt::make_self<DisplayAttributeEnum>(info_, index_);
        enumerator.as<IEnumTfDisplayAttributeInfo>().copy_to(ppEnum);
        return S_OK;
    }

    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override {
        if (!rgInfo) {
            return E_INVALIDARG;
        }
        if (ulCount != 1 && !pcFetched) {
            return E_INVALIDARG;
        }

        ULONG fetched = 0;
        while (fetched < ulCount && index_ == 0 && info_) {
            rgInfo[fetched] = info_.get();
            rgInfo[fetched]->AddRef();
            ++fetched;
            ++index_;
        }

        if (pcFetched) {
            *pcFetched = fetched;
        }

        return (fetched == ulCount) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG ulCount) override {
        if (ulCount == 0) {
            return S_OK;
        }
        if (index_ == 0) {
            index_ = 1;
            return (ulCount == 1) ? S_OK : S_FALSE;
        }
        return S_FALSE;
    }

private:
    winrt::com_ptr<ITfDisplayAttributeInfo> info_;
    ULONG index_ = 0;
};

HRESULT get_composition_display_attribute_atom(TfGuidAtom* atom) {
    if (!atom) {
        return E_INVALIDARG;
    }

    static TfGuidAtom cached_atom = TF_INVALID_GUIDATOM;
    if (cached_atom != TF_INVALID_GUIDATOM) {
        *atom = cached_atom;
        return S_OK;
    }

    ITfCategoryMgr* raw_category_mgr = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                        reinterpret_cast<void**>(&raw_category_mgr));
    if (FAILED(hr)) {
        return hr;
    }

    winrt::com_ptr<ITfCategoryMgr> category_mgr;
    category_mgr.attach(raw_category_mgr);

    const HRESULT register_hr = category_mgr->RegisterGUID(kCompositionDisplayAttributeGuid, &cached_atom);
    if (FAILED(register_hr)) {
        return register_hr;
    }

    *atom = cached_atom;
    return S_OK;
}

HRESULT apply_composition_display_attribute(ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!context || !range) {
        return E_INVALIDARG;
    }

    TfGuidAtom atom = TF_INVALID_GUIDATOM;
    HRESULT hr = get_composition_display_attribute_atom(&atom);
    if (FAILED(hr)) {
        return hr;
    }

    winrt::com_ptr<ITfProperty> attribute_property;
    hr = context->GetProperty(GUID_PROP_ATTRIBUTE, attribute_property.put());
    if (FAILED(hr)) {
        return hr;
    }

    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = atom;
    return attribute_property->SetValue(ec, range, &value);
}

}  // namespace

namespace tsf {

TextService::TextService() : candidate_ui_(std::make_unique<CandidateUiController>()) {}

TextService::~TextService() = default;

/**
 * @brief Implements ITfTextInputProcessor::Activate.
 *
 * Delegates activation to the shared setup path.
 */
STDMETHODIMP TextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) try {
    return activate(pThreadMgr, tfClientId);
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfTextInputProcessor::Deactivate.
 *
 * Tears down TSF state through the shared cleanup path.
 */
STDMETHODIMP TextService::Deactivate() try {
    deactivate();
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfTextInputProcessorEx::ActivateEx.
 *
 * Uses the same activation flow and currently ignores extra flags.
 */
STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD /*dwFlags*/) try {
    return activate(pThreadMgr, tfClientId);
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Shared activation helper.
 *
 * Attaches TSF sinks, stores thread manager state, and starts debug logging.
 */
HRESULT TextService::activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
    if (!pThreadMgr) return E_INVALIDARG;

    threadMgr.copy_from(pThreadMgr);
    _tfClientId = tfClientId;
    candidate_ui_->attach(pThreadMgr, tfClientId);

    winrt::com_ptr<ITfSource> itfSource;
    HRESULT hr = threadMgr->QueryInterface<ITfSource>(itfSource.put());
    if (FAILED(hr)) {
        deactivate();
        return hr;
    }

    hr = itfSource->AdviseSink(
        IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &dwThreadMgrEventSinkCookie);
    if (FAILED(hr)) {
        deactivate();
        return hr;
    }

    winrt::com_ptr<ITfKeystrokeMgr> itfKeystrokeMgr;
    hr = threadMgr->QueryInterface<ITfKeystrokeMgr>(itfKeystrokeMgr.put());
    if (FAILED(hr)) {
        deactivate();
        return hr;
    }

    hr = itfKeystrokeMgr->AdviseKeyEventSink(tfClientId, static_cast<ITfKeyEventSink*>(this), TRUE);
    if (FAILED(hr)) {
        deactivate();
        return hr;
    }

    DebugSink::instance().connect();
    DebugSink::instance().send(L"IME", L"Activated");
    ModeleManager::initialize();

    return S_OK;
}

/**
 * @brief Shared deactivation helper.
 *
 * Releases TSF sinks, clears composition state, and stops debug logging.
 */
void TextService::deactivate() {
    DebugSink::instance().send(L"IME", L"Deactivated");
    candidate_ui_->hide();
    DebugSink::instance().disconnect();

    if (itfComposition) {
        itfComposition->EndComposition(TF_INVALID_COOKIE);
        itfComposition = nullptr;
    }
    compositionBuffer.clear();

    if (threadMgr) {
        winrt::com_ptr<ITfKeystrokeMgr> itfKeystrokeMgr;
        if (SUCCEEDED(threadMgr->QueryInterface<ITfKeystrokeMgr>(itfKeystrokeMgr.put()))) {
            itfKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
        }

        if (dwThreadMgrEventSinkCookie != TF_INVALID_COOKIE) {
            winrt::com_ptr<ITfSource> pSource;
            if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(pSource.put())))) {
                pSource->UnadviseSink(dwThreadMgrEventSinkCookie);
            }
            dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
        }

        threadMgr = nullptr;
    }
    _tfClientId = TF_CLIENTID_NULL;
    candidate_ui_->detach();
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnInitDocumentMgr.
 *
 * No document-manager initialization is required yet.
 */
STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) try {
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnUninitDocumentMgr.
 *
 * No document-manager teardown is required yet.
 */
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) try {
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnSetFocus.
 *
 * Receives document focus changes but does not react to them yet.
 */
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* /*pDocMgrFocus*/,
                                     ITfDocumentMgr* /*pDocMgrPrevFocus*/) try {
    // TODO: Initialize or clear per-document state on focus switch.
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnPushContext.
 *
 * Accepts new contexts without additional bookkeeping.
 */
STDMETHODIMP TextService::OnPushContext(ITfContext* /*pContext*/) try {
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnPopContext.
 *
 * Releases contexts without additional cleanup.
 */
STDMETHODIMP TextService::OnPopContext(ITfContext* /*pContext*/) try {
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnSetFocus.
 *
 * Tracks foreground changes but currently keeps no extra state.
 */
STDMETHODIMP TextService::OnSetFocus(BOOL /*fForeground*/) try {
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnTestKeyDown.
 *
 * Reports whether the service intends to consume the key-down event.
 */
STDMETHODIMP TextService::OnTestKeyDown(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/,
                                        BOOL* pfEaten) try {
    if (!pfEaten) return E_INVALIDARG;
    DebugSink::instance().send(L"EVENT", L"OnTestKeyDown key=" + std::to_wstring(wParam));

    if (candidate_ui_->is_active()) {
        *pfEaten = candidate_ui_->can_handle_key(wParam) ? TRUE : FALSE;
        DebugSink::instance().send(
            L"EVENT", L"OnTestKeyDown candidate mode, eaten="s + (*pfEaten ? L"TRUE" : L"FALSE"));
        return S_OK;
    }

    if (!compositionBuffer.empty()) {
        const bool editing_key = (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_BACK ||
                                  wParam == VK_DOWN || wParam == VK_RIGHT);
        if (editing_key) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }

    *pfEaten = (Bopomofo::lookup(static_cast<int>(wParam)) != std::nullopt) ? TRUE : FALSE;
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnTestKeyUp.
 *
 * Leaves key-up events unhandled by default.
 */
STDMETHODIMP TextService::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                      BOOL* pfEaten) try {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnKeyDown.
 *
 * Updates the active composition or handles commit and cancel keys.
 */
STDMETHODIMP TextService::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) try {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    DebugSink::instance().send(L"EVENT", L"OnKeyDown");

    if (candidate_ui_->is_active()) {
        const CandidateKeyResult result = candidate_ui_->handle_key(wParam);
        switch (result) {
            case CandidateKeyResult::navigated:
                *pfEaten = TRUE;
                return S_OK;
            case CandidateKeyResult::finalized:
                refresh_composition_after_candidate_finalize(pContext);
                *pfEaten = TRUE;
                return S_OK;
            case CandidateKeyResult::aborted:
                *pfEaten = TRUE;
                return S_OK;
            case CandidateKeyResult::not_handled:
            default:
                break;
        }
    }

    if (wParam == VK_RETURN && !compositionBuffer.empty()) {
        DebugSink::instance().send(L"COMMIT", compositionBuffer.to_string());
        end_composition(pContext);
        *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_ESCAPE && itfComposition) {
        DebugSink::instance().send(L"CANCEL", compositionBuffer.to_string());
        compositionBuffer.clear();
        set_composition_text(pContext, L""_u16);
        end_composition(pContext);
        *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_BACK && !compositionBuffer.empty()) {
        compositionBuffer.remove_cur();
        if (compositionBuffer.empty()) {
            end_composition(pContext);
        } else {
            set_composition_text(pContext, compositionBuffer.to_string());
        }
        *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_DOWN && !compositionBuffer.empty()) {
        show_candidate_list_for_current_input(pContext, false);
        *pfEaten = TRUE;
        return S_OK;
    }

    if (wParam == VK_RIGHT && !compositionBuffer.empty()) {
        show_candidate_list_for_current_input(pContext, true);
        *pfEaten = TRUE;
        return S_OK;
    }
    auto cur_char = Bopomofo::lookup(static_cast<int>(wParam));
    if (cur_char == std::nullopt) {
        candidate_ui_->hide();
        // TODO: Handle non-Bopomofo keys.
        *pfEaten = FALSE;
        return S_OK;
    }

    // if (!itfComposition) {
    //     start_composition(pContext);
    // }
    compositionBuffer.add(cur_char.value());
    DebugSink::instance().send(L"KEY", compositionBuffer.to_string());
    set_composition_text(pContext, compositionBuffer.to_string());
    *pfEaten = TRUE;
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnKeyUp.
 *
 * Leaves key-up events unconsumed after key-down handling.
 */
STDMETHODIMP TextService::OnKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                  BOOL* pfEaten) try {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfKeyEventSink::OnPreservedKey.
 *
 * Declines preserved-key handling because no preserved keys are registered.
 */
STDMETHODIMP TextService::OnPreservedKey(ITfContext* /*pContext*/, REFGUID /*rguid*/, BOOL* pfEaten) try {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfCompositionSink::OnCompositionTerminated.
 *
 * Clears local composition state when TSF ends the composition externally.
 */
STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) try {
    candidate_ui_->hide();
    itfComposition = nullptr;
    compositionBuffer.clear();
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfDisplayAttributeProvider::EnumDisplayAttributeInfo.
 *
 * Returns not implemented because display attributes are not exposed yet.
 */
STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) try {
    if (!ppEnum) {
        return E_INVALIDARG;
    }

    *ppEnum = nullptr;
    auto info = winrt::make_self<CompositionDisplayAttributeInfo>();
    auto enumerator = winrt::make_self<DisplayAttributeEnum>(info.as<ITfDisplayAttributeInfo>());
    enumerator.as<IEnumTfDisplayAttributeInfo>().copy_to(ppEnum);
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Implements ITfDisplayAttributeProvider::GetDisplayAttributeInfo.
 *
 * Returns not implemented because no display attribute metadata is defined yet.
 */
STDMETHODIMP_(HRESULT __stdcall)
TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) try {
    if (!ppInfo) {
        return E_INVALIDARG;
    }

    *ppInfo = nullptr;
    if (!IsEqualGUID(guid, kCompositionDisplayAttributeGuid)) {
        return E_INVALIDARG;
    }

    auto info = winrt::make_self<CompositionDisplayAttributeInfo>();
    info.as<ITfDisplayAttributeInfo>().copy_to(ppInfo);
    return S_OK;
} catch (...) {
    return handle_com_exception();
}

/**
 * @brief Starts a new TSF composition session.
 *
 * Creates the TSF composition objects needed for a new input session.
 */
HRESULT TextService::start_composition(ITfContext* pContext) {
    if (!pContext) return E_INVALIDARG;
    if (itfComposition) return S_OK;

    winrt::com_ptr<ITfContextComposition> contextComposition;
    HRESULT hr = pContext->QueryInterface<ITfContextComposition>(contextComposition.put());
    if (FAILED(hr)) return hr;

    winrt::com_ptr<ITfInsertAtSelection> insertAtSelection;
    hr = pContext->QueryInterface<ITfInsertAtSelection>(insertAtSelection.put());
    if (FAILED(hr)) return hr;

    winrt::com_ptr<EditSession> editSession = winrt::make_self<EditSession>();

    editSession->set_operation([this, contextComposition, insertAtSelection](TfEditCookie ec) {
        winrt::com_ptr<ITfRange> range;
        if (FAILED(insertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, L"", 0, range.put()))) {
            return;
        }
        if (FAILED(contextComposition->StartComposition(
                ec, range.get(), static_cast<ITfCompositionSink*>(this), itfComposition.put()))) {
            return;
        }
    });

    HRESULT hrSession;
    pContext->RequestEditSession(_tfClientId, editSession.get(), TF_ES_READWRITE | TF_ES_SYNC, &hrSession) |
        win::check();

    return S_OK;
}

/**
 * @brief Ends the active TSF composition session.
 *
 * Clears the current composition object and buffered text.
 */
HRESULT TextService::end_composition(ITfContext* pContext) {
    candidate_ui_->hide();
    if (!itfComposition) return S_OK;

    winrt::com_ptr<EditSession> editSession = winrt::make_self<EditSession>();
    editSession->set_operation([this](TfEditCookie ec) {
        if (itfComposition) {
            itfComposition->EndComposition(ec);
            itfComposition = nullptr;
            compositionBuffer.clear();
        }
    });

    HRESULT hrSession;
    pContext->RequestEditSession(_tfClientId, editSession.get(), TF_ES_READWRITE | TF_ES_SYNC, &hrSession) |
        win::check();

    return S_OK;
}

/**
 * @brief Updates the active composition text.
 *
 * Applies the visible composition string to the current TSF context.
 */
HRESULT TextService::set_composition_text(ITfContext* pContext, const std::u16string& text) {
    if (!pContext) return E_INVALIDARG;

    winrt::com_ptr<ITfContextComposition> contextComposition;
    HRESULT hr = pContext->QueryInterface<ITfContextComposition>(contextComposition.put());
    if (FAILED(hr)) return hr;

    winrt::com_ptr<ITfInsertAtSelection> insertAtSelection;
    hr = pContext->QueryInterface<ITfInsertAtSelection>(insertAtSelection.put());
    if (FAILED(hr)) return hr;

    winrt::com_ptr<EditSession> editSession = winrt::make_self<EditSession>();
    editSession->set_operation([=, this](TfEditCookie ec) {
        winrt::com_ptr<ITfRange> range;
        if (!itfComposition) {
            insertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, range.put()) | win::check();
            contextComposition->StartComposition(
                ec, range.get(), static_cast<ITfCompositionSink*>(this), itfComposition.put()) |
                win::check();
        }

        range = nullptr;
        itfComposition->GetRange(range.put()) | win::check();
        range->SetText(ec, 0, convu16(text.data()), ULONG(text.size())) | win::check();

        apply_composition_display_attribute(pContext, ec, range.get()) | win::check();

        winrt::com_ptr<ITfRange> caret_range;
        range->Clone(caret_range.put()) | win::check();
        caret_range->Collapse(ec, TF_ANCHOR_END) | win::check();

        TF_SELECTION selection = {};
        selection.range = caret_range.get();
        selection.style.ase = TF_AE_END;
        selection.style.fInterimChar = FALSE;
        pContext->SetSelection(ec, 1, &selection) | win::check();
    });
    pContext->RequestEditSession(_tfClientId, editSession.get(), TF_ES_READWRITE | TF_ES_SYNC, &hr) | win::check();
    return S_OK;
}

void TextService::refresh_composition_after_candidate_finalize(ITfContext* pContext) {
    if (!pContext || compositionBuffer.empty()) {
        return;
    }

    DebugSink::instance().send(
        L"INFO", L"refresh_composition_after_candidate_finalize text="_u16 + compositionBuffer.to_string());
    set_composition_text(pContext, compositionBuffer.to_string());
}

void TextService::show_candidate_list_for_current_input(ITfContext* pContext, bool expand) {
    if (!pContext || compositionBuffer.empty()) {
        return;
    }

    auto& target = compositionBuffer.cur();
    show_candidate_list(target, pContext);
    if (expand) {
        candidate_ui_->expand();
    }
}

/**
 * @brief Displays the candidate list UI with the given candidates.
 */
void TextService::show_candidate_list(BopomofoPos& bopomofoPos, ITfContext* pContext) {
    // TODO
    std::shared_ptr<std::vector<std::wstring>> candidate_ptr = std::make_shared<std::vector<std::wstring>>();
    for (auto candi : bopomofoPos.get_candidates()) {
        std::u16string str;
        utf8::append16(candi, str);
        std::wstring wstr(str.begin(), str.end());
        candidate_ptr->push_back(wstr);
    }

    candidate_ui_->show(pContext, *candidate_ptr, [&bopomofoPos, candidate_ptr](std::wstring word) {
        auto& candidate = *candidate_ptr;
        for (int i = 0; i < candidate.size(); i++) {
            if (word == candidate[i]) {
                bopomofoPos.set_choose_index(i);
                return;
            }
        }
    });
}

}  // namespace tsf
