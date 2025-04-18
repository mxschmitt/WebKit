#include "DragSourceWin.h"
#include <windows.h>
#include <ole2.h>
#include <stdio.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(DragTargetWin);

DragSourceWin::DragSourceWin(HWND window)
    : m_refCount(1)
    , m_window(window)
{
    // Use m_window so that the compiler does not complain about an unused field.
    (void)m_window;
}

DragSourceWin::~DragSourceWin()
{
    // Cleanup if necessary.
}

HRESULT DragSourceWin::begin(IDataObject* dataObject, DWORD allowedEffects)
{
    if (!dataObject)
        return E_INVALIDARG;

    fprintf(stderr, "DragSourceWin::begin — DoDragDrop(allowed=0x%lx)\n", allowedEffects);
    DWORD effect = 0;
    HRESULT hr = ::DoDragDrop(dataObject, this, allowedEffects, &effect);
    fprintf(stderr, "  -> returned 0x%08lx, effect=0x%lx\n", hr, effect);
    return hr;
}

// IUnknown methods
HRESULT DragSourceWin::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
        *ppvObject = static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG DragSourceWin::AddRef(void)
{
    return InterlockedIncrement(&m_refCount);
}

ULONG DragSourceWin::Release(void)
{
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0)
        delete this;
    return refCount;
}

// IDropSource methods
HRESULT DragSourceWin::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState)
{
    // Cancel if escape is pressed.
    if (fEscapePressed)
        return DRAGDROP_S_CANCEL;
    
    // If left mouse button is no longer pressed, complete the drag.
    if (!(grfKeyState & MK_LBUTTON))
        return DRAGDROP_S_DROP;
    
    return S_OK;
}

HRESULT DragSourceWin::GiveFeedback(DWORD dwEffect)
{
    // Cast parameters to void to suppress unused warnings.
    (void)dwEffect;
    (void)m_window;
    
    // Use default cursors.
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

} // namespace WebKit
