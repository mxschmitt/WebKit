#pragma once

#include "config.h"
#include <windows.h>
#include <ole2.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/HashMap.h>
#include <oleidl.h>   // for IDataObject, IDropTarget, etc.
#include <WebCore/DragData.h>

namespace WebCore {
    typedef UncheckedKeyHashMap<unsigned, Vector<String>> DragDataMap;
}

namespace WebKit {

class WebPageProxy; // Forward declaration for event forwarding.

class DragTargetWin : public IDropTarget {
    WTF_MAKE_TZONE_ALLOCATED(DragTargetWin);
public:
    // Constructs a drop target for the given window and associated WebPageProxy.
    DragTargetWin(HWND window, WebKit::WebPageProxy*);
    virtual ~DragTargetWin();

    // IUnknown interface:
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    virtual ULONG   STDMETHODCALLTYPE AddRef(void) override;
    virtual ULONG   STDMETHODCALLTYPE Release(void) override;

    // IDropTarget interface methods:
    virtual HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    virtual HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    virtual HRESULT STDMETHODCALLTYPE DragLeave(void) override;
    virtual HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

    void didPerformDragOperation();
private:
    LONG m_refCount;
    HWND m_window;               // The window that acts as the drop target.
    WebKit::WebPageProxy* m_page; // where we forward drag messages

    // stash the last Entered drag data so Over/Leave/Drop can all use it
    WebCore::DragData m_dragData;
};

} // namespace WebKit
