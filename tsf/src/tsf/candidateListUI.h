#pragma once

#include <msctf.h>
#include <winrt/base.h>

#include "system/sysutil.hpp"

namespace tsf {
// clang-format off
class CandidateListUIElement
    : winrt::implements<
        CandidateListUIElement, 
        ITfCandidateListUIElement, 
        ITfCandidateListUIElementBehavior>
    , public module_lock_updater {
    // clang-format on
public:
    // ITfCandidateListUIElement
    HRESULT GetSelection(UINT *pnIndex) override;
    HRESULT GetCount(UINT *pnCount) override;
    HRESULT GetString(UINT uIndex, BSTR *pbstr) override;
    HRESULT GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt) override;
    HRESULT SetPageIndex(UINT *pIndex, UINT uPageCnt) override;
    HRESULT GetCurrentPage(UINT *puPage) override;

    // ITfCandidateListUIElementBehavior
    HRESULT SetSelection(UINT nIndex) override;
    HRESULT Finalize() override;
    HRESULT Abort() override;
};
}  // namespace tsf