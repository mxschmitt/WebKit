
#include "config.h"
#include "DragTargetWin.h"
#include <windows.h>
#include <ole2.h>
#include <stdio.h>
#include <wtf/TZoneMallocInlines.h>
#include <WebCore/DragData.h>
#include <WebCore/DragActions.h>
#include "SandboxExtension.h"
#include "WebPageProxy.h"

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(DragTargetWin);

DragTargetWin::DragTargetWin(HWND window, WebPageProxy* page)
    : m_refCount(1)
    , m_window(window)
    , m_page(page)
{
    // Use m_window and m_page to prevent unused field warnings.
    (void)m_window;
}

DragTargetWin::~DragTargetWin()
{
    // Cleanup if necessary.
}

// IUnknown interface
HRESULT DragTargetWin::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG DragTargetWin::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG DragTargetWin::Release()
{
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0)
        delete this;
    return refCount;
}

// IDropTarget interface methods

HRESULT DragTargetWin::DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    // Cast unused parameters to void.
    (void)grfKeyState;
    (void)pDataObj;

    // 1) Remember the raw screen point
    POINT screenPt = { pt.x, pt.y };

    // 2) Convert to client‑area coords
    POINT clientPt = screenPt;
    ::ScreenToClient(m_window, &clientPt);

    WebCore::IntPoint clientPosition(clientPt.x, clientPt.y);
    WebCore::IntPoint globalPosition(screenPt.x, screenPt.y);
    fprintf(stderr, "clientPt: (%ld, %ld)\n", clientPt.x, clientPt.y);
    fprintf(stderr, "screenPt: (%ld, %ld)\n", screenPt.x, screenPt.y);
    WebCore::DragDataMap m_dragDataMap;
    WebCore::DragData dragData(
        pDataObj,                            // IDataObject*
        clientPosition,                            // clientPosition
        globalPosition,                            // globalPosition
        WebCore::DragOperation::Move,       // sourceOperationMask
        { },                                 // applicationFlags (none)
        WebCore::anyDragDestinationAction()  // <-- here’s your destinationAction
    );

    m_dragData = dragData;
    m_page->dragEntered(dragData);
    fprintf(stderr, "DragTargetWin: DragEnter at (%ld, %ld)\n", pt.x, pt.y);

    // For now, we simply allow copy.
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

HRESULT DragTargetWin::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    (void)grfKeyState;
    
    POINT screenPt = { pt.x, pt.y };
    POINT clientPt = screenPt;
    ::ScreenToClient(m_window, &clientPt);

    WebCore::IntPoint clientPosition(clientPt.x, clientPt.y);

    fprintf(stderr, "DragTargetWin: DragOver at client position (%ld, %ld)\n", clientPt.x, clientPt.y);

    // 2) Update the DragData you created in DragEnter
    m_dragData.setClientPosition(clientPosition);

    m_page->dragUpdated(m_dragData);

    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

HRESULT DragTargetWin::DragLeave()
{
    fprintf(stderr, "DragTargetWin: DragLeave\n");
    m_page->dragExited(m_dragData);
    return S_OK;
}

HRESULT DragTargetWin::Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    (void)pDataObj;
    (void)grfKeyState;

    fprintf(stderr, "DragTargetWin: Drop at (%ld, %ld)\n", pt.x, pt.y);
    m_page->performDragOperation(m_dragData, { }, { }, { });

    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

void DragTargetWin::didPerformDragOperation()
{
    m_page->dragEnded(m_dragData.clientPosition(), m_dragData.globalPosition(), WebCore::DragOperation::Move);
}

} // namespace WebKit
