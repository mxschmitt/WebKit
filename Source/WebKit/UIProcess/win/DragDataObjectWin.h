// DragDataObjectWin.h
#pragma once

#include "config.h"
#include <windows.h>
#include <objidl.h> // for IDataObject
#include <oleidl.h> // OLE definitions
#include <wtf/FastMalloc.h>
#include <wtf/HashMap.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
    typedef UncheckedKeyHashMap<unsigned, Vector<String>> DragDataMap;
}

namespace WebKit {

class DragDataObjectWin : public IDataObject {
    WTF_MAKE_FAST_ALLOCATED(DragDataObjectWin);
public:
    // Construct from a DragDataMap representing available formats.
    explicit DragDataObjectWin(WebCore::DragDataMap&& map);
    virtual ~DragDataObjectWin();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pFmtEtc, STGMEDIUM* pMedium) override;
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pFmtEtc, STGMEDIUM* pMedium) override;
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pFmtEtc) override;
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pFmtEtcIn, FORMATETC* pFmtEtcOut) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pFmtEtc, STGMEDIUM* pMedium, BOOL fRelease) override;
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc) override;
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* pFmtEtc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override;
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection) override;
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** ppEnumAdvise) override;

private:
    LONG m_refCount { 1 };
    WebCore::DragDataMap m_map;
};

} // namespace WebKit
