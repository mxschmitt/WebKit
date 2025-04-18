#pragma once

#include "config.h"
#include <windows.h>
#include <ole2.h>
#include <shobjidl.h> // For IDragSourceHelper
#include <wtf/TZoneMalloc.h>

namespace WebKit {

class DragSourceWin : public IDropSource {
    WTF_MAKE_TZONE_ALLOCATED(DragSourceWin);
public:
    // Constructs a drag source for the given window handle.
    explicit DragSourceWin(HWND window);
    virtual ~DragSourceWin();

    // Initiates the drag-drop operation.
    // dataObjectHelper is expected to support IDragSourceHelper (and via QueryInterface, IDataObject).
    // allowedEffects is a combination of DROPEFFECT_* flags.
    HRESULT begin(IDataObject* dataObject, DWORD allowedEffects);

    // IUnknown interface:
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    virtual ULONG   STDMETHODCALLTYPE AddRef(void) override;
    virtual ULONG   STDMETHODCALLTYPE Release(void) override;

    // IDropSource interface methods:
    virtual HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override;
    virtual HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override;

private:
    LONG m_refCount;
    HWND m_window; // The window from which the drag originates.
};

} // namespace WebKit
