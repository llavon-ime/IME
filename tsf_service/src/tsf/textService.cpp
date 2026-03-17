#include "tsf/textService.h"

#include <new>

#include "core/bopomofo.h"
#include "debugSink.h"
#include "tsf/globals.h"
#include "tsf/textService.h"

/**
 * @brief CTextService constructor.
 *
 * Increments the module reference count for this service instance.
 */
CTextService::CTextService() {
    ++g_cDllRef;
}

/**
 * @brief CTextService destructor.
 *
 * Decrements the module reference count when the service is destroyed.
 */
CTextService::~CTextService() {
    --g_cDllRef;
}

/**
 * @brief Implements IUnknown::QueryInterface.
 *
 * Returns the requested COM interface when this service supports it.
 */
STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    }

    if (*ppv) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

/**
 * @brief Implements IUnknown::AddRef.
 *
 * Increments the COM reference count for this object.
 */
STDMETHODIMP_(ULONG) CTextService::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&_cRef));
}

/**
 * @brief Implements IUnknown::Release.
 *
 * Decrements the COM reference count and deletes the object at zero.
 */
STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG cRef = InterlockedDecrement(&_cRef);
    if (cRef == 0) delete this;
    return static_cast<ULONG>(cRef);
}

/**
 * @brief Implements ITfTextInputProcessor::Activate.
 *
 * Delegates activation to the shared setup path.
 */
STDMETHODIMP CTextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
    return _Activate(pThreadMgr, tfClientId);
}

/**
 * @brief Implements ITfTextInputProcessor::Deactivate.
 *
 * Tears down TSF state through the shared cleanup path.
 */
STDMETHODIMP CTextService::Deactivate() {
    _Deactivate();
    return S_OK;
}

/**
 * @brief Implements ITfTextInputProcessorEx::ActivateEx.
 *
 * Uses the same activation flow and currently ignores extra flags.
 */
STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD /*dwFlags*/) {
    return _Activate(pThreadMgr, tfClientId);
}

/**
 * @brief Shared activation helper.
 *
 * Attaches TSF sinks, stores thread manager state, and starts debug logging.
 */
HRESULT CTextService::_Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
    RETURN_IF_FAILED(pThreadMgr->QueryInterface(IID_PPV_ARGS(_pThreadMgr.put())));
    _tfClientId = tfClientId;

    // 失敗時自動回滾，RETURN_IF_FAILED 觸發早返時 scope_exit 會呼叫 _Deactivate
    auto cleanup = wil::scope_exit([&] {
        _Deactivate();
    });

    // ── 訂閱 ITfThreadMgrEventSink ────────────────────────────
    {
        wil::com_ptr_nothrow<ITfSource> pSource;
        RETURN_IF_FAILED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(pSource.put())));
        RETURN_IF_FAILED(pSource->AdviseSink(
            IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &_dwThreadMgrEventSinkCookie));
    }

    // ── 訂閱 ITfKeyEventSink ──────────────────────────────────
    {
        wil::com_ptr_nothrow<ITfKeystrokeMgr> pKeystrokeMgr;
        RETURN_IF_FAILED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(pKeystrokeMgr.put())));
        RETURN_IF_FAILED(pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, static_cast<ITfKeyEventSink*>(this), TRUE));
    }

    cleanup.release();

    // ── 偵錯：連線至 Python 接收端 ───────────────────────────
    DebugSink::instance().connect();
    DebugSink::instance().send(L"IME", L"Activated");

    return S_OK;
}

/**
 * @brief Shared deactivation helper.
 *
 * Releases TSF sinks, clears composition state, and stops debug logging.
 */
void CTextService::_Deactivate() {
    // ── 偵錯：通知 Python 並斷線 ─────────────────────────────
    DebugSink::instance().send(L"IME", L"Deactivated");
    DebugSink::instance().disconnect();

    // 結束組字
    if (_pComposition) {
        _pComposition->EndComposition(TF_INVALID_COOKIE);
        _pComposition.reset();
    }
    _compositionBuffer.clear();

    if (_pThreadMgr) {
        // 取消訂閱 ITfKeyEventSink
        if (wil::com_ptr_nothrow<ITfKeystrokeMgr> pKM;
            SUCCEEDED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(pKM.put())))) {
            pKM->UnadviseKeyEventSink(_tfClientId);
        }

        // 取消訂閱 ITfThreadMgrEventSink
        if (_dwThreadMgrEventSinkCookie != TF_INVALID_COOKIE) {
            if (wil::com_ptr_nothrow<ITfSource> pSource;
                SUCCEEDED(_pThreadMgr->QueryInterface(IID_PPV_ARGS(pSource.put())))) {
                pSource->UnadviseSink(_dwThreadMgrEventSinkCookie);
            }
            _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
        }

        _pThreadMgr.reset();
    }
    _tfClientId = TF_CLIENTID_NULL;
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnInitDocumentMgr.
 *
 * No document-manager initialization is required yet.
 */
STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) {
    return S_OK;
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnUninitDocumentMgr.
 *
 * No document-manager teardown is required yet.
 */
STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) {
    return S_OK;
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnSetFocus.
 *
 * Receives document focus changes but does not react to them yet.
 */
STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* /*pDocMgrFocus*/, ITfDocumentMgr* /*pDocMgrPrevFocus*/) {
    // TODO: 焦點切換時可在此初始化 / 清除每文件的狀態
    return S_OK;
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnPushContext.
 *
 * Accepts new contexts without additional bookkeeping.
 */
STDMETHODIMP CTextService::OnPushContext(ITfContext* /*pContext*/) {
    return S_OK;
}

/**
 * @brief Implements ITfThreadMgrEventSink::OnPopContext.
 *
 * Releases contexts without additional cleanup.
 */
STDMETHODIMP CTextService::OnPopContext(ITfContext* /*pContext*/) {
    return S_OK;
}

/**
 * @brief Implements ITfKeyEventSink::OnSetFocus.
 *
 * Tracks foreground changes but currently keeps no extra state.
 */
STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/) {
    return S_OK;
}

/**
 * @brief Implements ITfKeyEventSink::OnTestKeyDown.
 *
 * Reports whether the service intends to consume the key-down event.
 */
STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    // TODO: 判斷此按鍵是否由 IME 消耗。
    //       OnKeyDown 用 ToUnicodeEx 轉換字元，這裡只需判斷 VK 範圍即可。
    *pfEaten = (wParam >= 'A' && wParam <= 'Z') ? TRUE : FALSE;
    return S_OK;
}

/**
 * @brief Implements ITfKeyEventSink::OnTestKeyUp.
 *
 * Leaves key-up events unhandled by default.
 */
STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

/**
 * @brief Implements ITfKeyEventSink::OnKeyDown.
 *
 * Updates the active composition or handles commit and cancel keys.
 */
STDMETHODIMP CTextService::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    // ── Enter：送出組字 ─────────────────────────────────────
    if (wParam == VK_RETURN && !_compositionBuffer.empty()) {
        DebugSink::instance().send(L"COMMIT", _compositionBuffer);
        _EndComposition(pContext);
        *pfEaten = TRUE;
        return S_OK;
    }

    // ── Escape：取消組字 ────────────────────────────────────
    if (wParam == VK_ESCAPE && _pComposition) {
        DebugSink::instance().send(L"CANCEL", _compositionBuffer);
        _compositionBuffer.clear();
        _SetCompositionText(pContext, L"");
        _EndComposition(pContext);
        *pfEaten = TRUE;
        return S_OK;
    }

    // ── Backspace：刪除最後一個字元 ─────────────────────────
    if (wParam == VK_BACK && !_compositionBuffer.empty()) {
        _compositionBuffer.pop_back();
        if (_compositionBuffer.empty())
            _EndComposition(pContext);
        else
            _SetCompositionText(pContext, _compositionBuffer);
        *pfEaten = TRUE;
        return S_OK;
    }

    auto cur_char = Bopomofo::lookup(static_cast<int>(wParam));
    if (cur_char == std::nullopt) {
        // 非注音
        // TODO
        return S_OK;
    } else {
        if (!_pComposition) {
            _StartComposition(pContext);
        }
        _compositionBuffer.push_back(cur_char.value());
        DebugSink::instance().send(L"KEY", _compositionBuffer);
        _SetCompositionText(pContext, _compositionBuffer);
        *pfEaten = TRUE;
        return S_OK;
    }
}

/**
 * @brief Implements ITfKeyEventSink::OnKeyUp.
 *
 * Leaves key-up events unconsumed after key-down handling.
 */
STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

/**
 * @brief Implements ITfKeyEventSink::OnPreservedKey.
 *
 * Declines preserved-key handling because no preserved keys are registered.
 */
STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pContext*/, REFGUID /*rguid*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

/**
 * @brief Implements ITfCompositionSink::OnCompositionTerminated.
 *
 * Clears local composition state when TSF ends the composition externally.
 */
STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/) {
    _pComposition.reset();
    _compositionBuffer.clear();
    return S_OK;
}

/**
 * @brief Implements ITfDisplayAttributeProvider::EnumDisplayAttributeInfo.
 *
 * Returns not implemented because display attributes are not exposed yet.
 */
STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    (void)ppEnum;
    return E_NOTIMPL;
}

/**
 * @brief Implements ITfDisplayAttributeProvider::GetDisplayAttributeInfo.
 *
 * Returns not implemented because no display attribute metadata is defined yet.
 */
STDMETHODIMP_(HRESULT __stdcall) CTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    (void)guid;
    (void)ppInfo;
    return E_NOTIMPL;
}

/**
 * @brief Starts a new TSF composition session.
 *
 * Creates the TSF composition objects needed for a new input session.
 */
HRESULT CTextService::_StartComposition(ITfContext* pContext) {
    if (_pComposition) return S_OK;  // 已在組字中

    wil::com_ptr_nothrow<ITfContextComposition> pContextComposition;
    RETURN_IF_FAILED(pContext->QueryInterface(IID_PPV_ARGS(pContextComposition.put())));

    wil::com_ptr_nothrow<ITfInsertAtSelection> pInsertAtSelection;
    RETURN_IF_FAILED(pContext->QueryInterface(IID_PPV_ARGS(pInsertAtSelection.put())));

    // TODO: 實作完整的 StartComposition + EditSession
    return S_OK;
}

/**
 * @brief Ends the active TSF composition session.
 *
 * Clears the current composition object and buffered text.
 */
HRESULT CTextService::_EndComposition(ITfContext* /*pContext*/) {
    if (!_pComposition) return S_OK;

    // TODO: 以 EditSession 送出最終文字後再結束組字
    _pComposition->EndComposition(TF_INVALID_COOKIE);
    _pComposition.reset();
    _compositionBuffer.clear();
    return S_OK;
}

/**
 * @brief Updates the active composition text.
 *
 * Applies the visible composition string to the current TSF context.
 */
HRESULT CTextService::_SetCompositionText(ITfContext* /*pContext*/, const std::wstring& /*text*/) {
    // TODO: 以 ITfRange + ITfProperty 更新組字字串與顯示屬性
    return S_OK;
}
