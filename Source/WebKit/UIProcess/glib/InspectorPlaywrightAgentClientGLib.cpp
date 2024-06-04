/*
 * Copyright (C) 2019 Microsoft Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InspectorPlaywrightAgentClientGLib.h"

#if ENABLE(REMOTE_INSPECTOR)

#include "InspectorPlaywrightAgent.h"
#include "PageClient.h"
#include "ViewSnapshotStore.h"
#include "WebKitBrowserInspectorPrivate.h"
#include "WebKitWebContextPrivate.h"
#include "WebKitWebsiteDataManagerPrivate.h"
#include "WebKitWebViewPrivate.h"
#include "WebPageProxy.h"
#include <WebCore/ImageBufferUtilitiesCairo.h>
#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/text/Base64.h>
#include <wtf/text/StringView.h>
#include <wtf/text/WTFString.h>


namespace WebKit {

static WebCore::SoupNetworkProxySettings parseRawProxySettings(const String& proxyServer, const char* const* ignoreHosts)
{
    WebCore::SoupNetworkProxySettings settings;
    if (proxyServer.isEmpty())
        return settings;

    settings.mode = WebCore::SoupNetworkProxySettings::Mode::Custom;
    settings.defaultProxyURL = proxyServer.utf8();
    settings.ignoreHosts.reset(g_strdupv(const_cast<char**>(ignoreHosts)));
    return settings;
}

static WebCore::SoupNetworkProxySettings parseProxySettings(const String& proxyServer, const String& proxyBypassList)
{
    Vector<const char*> ignoreHosts;
    if (!proxyBypassList.isEmpty()) {
        Vector<String> tokens = proxyBypassList.split(',');
        Vector<CString> protectTokens;
        for (String token : tokens) {
            CString cstr = token.utf8();
            ignoreHosts.append(cstr.data());
            protectTokens.append(WTFMove(cstr));
        }
    }
    ignoreHosts.append(nullptr);
    return parseRawProxySettings(proxyServer, ignoreHosts.data());
}

InspectorPlaywrightAgentClientGlib::InspectorPlaywrightAgentClientGlib(const WTF::String& proxyURI, const char* const* ignoreHosts)
    : m_proxySettings(parseRawProxySettings(proxyURI, ignoreHosts))
{
}

RefPtr<WebPageProxy> InspectorPlaywrightAgentClientGlib::createPage(WTF::String& error, const BrowserContext& browserContext)
{
    auto sessionID = browserContext.dataStore->sessionID();
    WebKitWebContext* context = m_idToContext.get(sessionID);
    if (!context && !browserContext.dataStore->isPersistent()) {
        ASSERT_NOT_REACHED();
        error = "Context with provided id not found"_s;
        return nullptr;
    }

    RefPtr<WebPageProxy> page = webkitBrowserInspectorCreateNewPageInContext(context);
    if (page == nullptr) {
        error = "Failed to create new page in the context"_s;
        return nullptr;
    }

    if (context == nullptr && sessionID != page->sessionID()) {
        ASSERT_NOT_REACHED();
        error = " Failed to create new page in default context"_s;
        return nullptr;
    }

    return page;
}

void InspectorPlaywrightAgentClientGlib::closeBrowser()
{
    m_idToContext.clear();
    webkitBrowserInspectorQuitApplication();
    if (webkitWebContextExistingCount() > 1)
        fprintf(stderr, "LEAK: %d contexts are still alive when closing browser\n", webkitWebContextExistingCount());
}

std::unique_ptr<BrowserContext> InspectorPlaywrightAgentClientGlib::createBrowserContext(WTF::String& error, const WTF::String& proxyServer, const WTF::String& proxyBypassList)
{
#if !ENABLE(2022_GLIB_API)
    GRefPtr<WebKitWebsiteDataManager> data_manager = adoptGRef(webkit_website_data_manager_new_ephemeral());
#endif
    GRefPtr<WebKitWebContext> context = adoptGRef(WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT,
#if !ENABLE(2022_GLIB_API)
    "website-data-manager", data_manager.get(),
#endif
    // WPE has PSON enabled by default and doesn't have such parameter.
#if PLATFORM(GTK)
        "process-swap-on-cross-site-navigation-enabled", true,
#endif
        nullptr)));
    if (!context) {
        error = "Failed to create GLib ephemeral context"_s;
        return nullptr;
    }

#if ENABLE(2022_GLIB_API)
    GRefPtr<WebKitNetworkSession> networkSession = adoptGRef(webkit_network_session_new_ephemeral());
    webkit_web_context_set_network_session_for_automation(context.get(), networkSession.get());
    GRefPtr<WebKitWebsiteDataManager> data_manager = webkit_network_session_get_website_data_manager(networkSession.get());
#endif

    auto browserContext = std::make_unique<BrowserContext>();
    browserContext->processPool = &webkitWebContextGetProcessPool(context.get());
    browserContext->dataStore = &webkitWebsiteDataManagerGetDataStore(data_manager.get());
    PAL::SessionID sessionID = browserContext.get()->dataStore->sessionID();
    m_idToContext.set(sessionID, WTFMove(context));

    if (!proxyServer.isEmpty()) {
        WebCore::SoupNetworkProxySettings contextProxySettings = parseProxySettings(proxyServer, proxyBypassList);
        browserContext->dataStore->setNetworkProxySettings(WTFMove(contextProxySettings));
    } else {
        browserContext->dataStore->setNetworkProxySettings(WebCore::SoupNetworkProxySettings(m_proxySettings));
    }
    return browserContext;
}

void InspectorPlaywrightAgentClientGlib::deleteBrowserContext(WTF::String& error, PAL::SessionID sessionID)
{
    m_idToContext.remove(sessionID);
}

void InspectorPlaywrightAgentClientGlib::takePageScreenshot(WebPageProxy& page, WebCore::IntRect&& clip, bool nominalResolution, CompletionHandler<void(const String&, const String&)>&& completionHandler)
{
    page.callAfterNextPresentationUpdate([protectedPage = Ref{ page }, clip = WTFMove(clip), nominalResolution, completionHandler = WTFMove(completionHandler)]() mutable {
        cairo_surface_t* surface = nullptr;
#if PLATFORM(GTK)
        RefPtr<ViewSnapshot> viewSnapshot = protectedPage->pageClient().takeViewSnapshot(WTFMove(clip), nominalResolution);
        if (viewSnapshot)
            surface = viewSnapshot->surface();
#elif PLATFORM(WPE)
        RefPtr<cairo_surface_t> protectPtr = protectedPage->pageClient().takeViewSnapshot(WTFMove(clip), nominalResolution);
        surface = protectPtr.get();
#endif
        if (surface) {
            Vector<uint8_t> encodeData = WebCore::encodeData(surface, "image/png"_s, std::nullopt);
            completionHandler(emptyString(), makeString("data:image/png;base64,"_s, base64Encoded(encodeData)));
            return;
        }

        completionHandler("Failed to take screenshot"_s, emptyString());
    });
}

} // namespace WebKit

#endif // ENABLE(REMOTE_INSPECTOR)
