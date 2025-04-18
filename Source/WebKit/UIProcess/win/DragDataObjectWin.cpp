#include "DragDataObjectWin.h"
#include <windows.h>
#include <oleidl.h>
#include <stdio.h>

namespace WebKit {

DragDataObjectWin::DragDataObjectWin(WebCore::DragDataMap&& map)
    : m_map(WTFMove(map))
{
}

DragDataObjectWin::~DragDataObjectWin()
{
}

HRESULT DragDataObjectWin::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
        *ppvObject = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG DragDataObjectWin::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG DragDataObjectWin::Release()
{
    ULONG c = InterlockedDecrement(&m_refCount);
    if (!c)
        delete this;
    return c;
}

HRESULT DragDataObjectWin::QueryGetData(FORMATETC* pFmtEtc)
{
    if (!pFmtEtc)
        return E_INVALIDARG;
    // Check if this format is in our map
    return m_map.contains(pFmtEtc->cfFormat) ? S_OK : DV_E_FORMATETC;
}

HRESULT DragDataObjectWin::GetData(FORMATETC* pFmtEtc, STGMEDIUM* pMedium)
{
    if (!pFmtEtc || !pMedium)
        return E_INVALIDARG;
    return DV_E_FORMATETC;
}

HRESULT DragDataObjectWin::GetDataHere(FORMATETC* pFmtEtc, STGMEDIUM* pMedium)
{
    (void)pFmtEtc;
    (void)pMedium;
    return E_NOTIMPL;
}

HRESULT DragDataObjectWin::GetCanonicalFormatEtc(FORMATETC* pFmtEtcIn, FORMATETC* pFmtEtcOut)
{
    (void)pFmtEtcIn;
    (void)pFmtEtcOut;
    return E_NOTIMPL;
}

HRESULT DragDataObjectWin::SetData(FORMATETC* pFmtEtc, STGMEDIUM* pMedium, BOOL fRelease)
{
    (void)pFmtEtc;
    (void)pMedium;
    (void)fRelease;
    return E_NOTIMPL;
}

HRESULT DragDataObjectWin::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc)
{
    (void)dwDirection;
    (void)ppEnumFormatEtc;
    return E_NOTIMPL;
}

HRESULT DragDataObjectWin::DAdvise(FORMATETC* pFmtEtc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection)
{
    (void)pFmtEtc;
    (void)advf;
    (void)pAdvSink;
    (void)pdwConnection;
    return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT DragDataObjectWin::DUnadvise(DWORD dwConnection)
{
    (void)dwConnection;
    return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT DragDataObjectWin::EnumDAdvise(IEnumSTATDATA** ppEnumAdvise)
{
    (void)ppEnumAdvise;
    return OLE_E_ADVISENOTSUPPORTED;
}
} // namespace WebKit
