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
#include "InspectorPlaywrightAgent.h"

#if ENABLE(REMOTE_INSPECTOR)

#include "APIGeolocationProvider.h"
#include "APIHTTPCookieStore.h"
#include "APIPageConfiguration.h"
#include "FrameInfoData.h"
#include "InspectorPlaywrightAgentClient.h"
#include "InspectorTargetProxy.h"
#include "NetworkProcessMessages.h"
#include "NetworkProcessProxy.h"
#include "PageClient.h"
#include "PlaywrightFullScreenManagerProxyClient.h"
#include "SandboxExtension.h"
#include "StorageNamespaceIdentifier.h"
#include "WebAutomationSession.h"
#include "WebGeolocationManagerProxy.h"
#include "WebGeolocationPosition.h"
#include "WebFrameProxy.h"
#include "WebInspectorUtilities.h"
#include "WebPageGroup.h"
#include "WebPageInspectorController.h"
#include "WebPageInspectorTarget.h"
#include "WebPageMessages.h"
#include "WebPageProxy.h"
#include "WebProcessPool.h"
#include "WebProcessProxy.h"
#include "WebsiteDataRecord.h"
#include <WebCore/FrameIdentifier.h>
#include <WebCore/GeolocationPositionData.h>
#include <WebCore/InspectorPageAgent.h>
#include <WebCore/ProcessIdentifier.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/WindowFeatures.h>
#include <JavaScriptCore/InspectorBackendDispatcher.h>
#include <JavaScriptCore/InspectorFrontendChannel.h>
#include <JavaScriptCore/InspectorFrontendRouter.h>
#include <pal/SessionID.h>
#include <stdlib.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/HexNumber.h>
#include <wtf/URL.h>

using namespace Inspector;

namespace WebKit {

class InspectorPlaywrightAgent::PageProxyChannel : public FrontendChannel {
    WTF_MAKE_FAST_ALLOCATED;
public:
    PageProxyChannel(FrontendChannel& frontendChannel, String browserContextID, String pageProxyID, WebPageProxy& page)
        : m_browserContextID(browserContextID)
        , m_pageProxyID(pageProxyID)
        , m_frontendChannel(frontendChannel)
        , m_page(page)
    {
    }

    ~PageProxyChannel() override = default;

    void dispatchMessageFromFrontend(const String& message)
    {
        m_page.inspectorController().dispatchMessageFromFrontend(message);
    }

    WebPageProxy& page() { return m_page; }

    void disconnect()
    {
        m_page.inspectorController().disconnectFrontend(*this);
    }

private:
    ConnectionType connectionType() const override { return m_frontendChannel.connectionType(); }
    void sendMessageToFrontend(const String& message) override
    {
        m_frontendChannel.sendMessageToFrontend(addTabIdToMessage(message));
    }

    String addTabIdToMessage(const String& message) {
        RefPtr<JSON::Value> parsedMessage = JSON::Value::parseJSON(message);
        if (!parsedMessage)
            return message;

        RefPtr<JSON::Object> messageObject = parsedMessage->asObject();
        if (!messageObject)
            return message;

        messageObject->setString("browserContextId"_s, m_browserContextID);
        messageObject->setString("pageProxyId"_s, m_pageProxyID);
        return messageObject->toJSONString();
    }

    String m_browserContextID;
    String m_pageProxyID;
    FrontendChannel& m_frontendChannel;
    WebPageProxy& m_page;
};

class OverridenGeolocationProvider final : public API::GeolocationProvider, public CanMakeWeakPtr<OverridenGeolocationProvider> {
     WTF_MAKE_NONCOPYABLE(OverridenGeolocationProvider);
public:
    OverridenGeolocationProvider()
        : m_position(WebGeolocationPosition::create(WebCore::GeolocationPositionData()))
    {
    }

    void setPosition(const Ref<WebGeolocationPosition>& position) {
        m_position = position;
    }

private:
    void startUpdating(WebGeolocationManagerProxy& proxy) override
    {
        proxy.providerDidChangePosition(&m_position.get());
    }

    void stopUpdating(WebGeolocationManagerProxy&) override
    {
    }

    void setEnableHighAccuracy(WebGeolocationManagerProxy&, bool enabled) override
    {
    }

    Ref<WebGeolocationPosition> m_position;
};

namespace {

void setGeolocationProvider(BrowserContext* browserContext) {
    auto provider = makeUnique<OverridenGeolocationProvider>();
    browserContext->geolocationProvider = *provider;
    auto* geoManager = browserContext->processPool->supplement<WebGeolocationManagerProxy>();
    geoManager->setProvider(WTFMove(provider));
}

String toBrowserContextIDProtocolString(const PAL::SessionID& sessionID)
{
    StringBuilder builder;
    builder.append(hex(sessionID.toUInt64(), 16));
    return builder.toString();
}

String toPageProxyIDProtocolString(const WebPageProxy& page)
{
    return makeString(page.identifier().toUInt64());
}


static Ref<JSON::ArrayOf<String>> getEnabledWindowFeatures(const WebCore::WindowFeatures& features) {
  auto result = JSON::ArrayOf<String>::create();
  if (features.x)
    result->addItem("left=" + String::number(*features.x));
  if (features.y)
    result->addItem("top=" + String::number(*features.y));
  if (features.width)
    result->addItem("width=" + String::number(*features.width));
  if (features.height)
    result->addItem("height=" + String::number(*features.height));
  if (features.menuBarVisible)
    result->addItem("menubar"_s);
  if (features.toolBarVisible)
    result->addItem("toolbar"_s);
  if (features.statusBarVisible)
    result->addItem("status"_s);
  if (features.locationBarVisible)
    result->addItem("location"_s);
  if (features.scrollbarsVisible)
    result->addItem("scrollbars"_s);
  if (features.resizable)
    result->addItem("resizable"_s);
  if (features.fullscreen)
    result->addItem("fullscreen"_s);
  if (features.dialog)
    result->addItem("dialog"_s);
  if (features.noopener)
    result->addItem("noopener"_s);
  if (features.noreferrer)
    result->addItem("noreferrer"_s);
  for (const auto& additionalFeature : features.additionalFeatures)
    result->addItem(additionalFeature);
  return result;
}

Inspector::Protocol::Playwright::CookieSameSitePolicy cookieSameSitePolicy(WebCore::Cookie::SameSitePolicy policy)
{
    switch (policy) {
    case WebCore::Cookie::SameSitePolicy::None:
        return Inspector::Protocol::Playwright::CookieSameSitePolicy::None;
    case WebCore::Cookie::SameSitePolicy::Lax:
        return Inspector::Protocol::Playwright::CookieSameSitePolicy::Lax;
    case WebCore::Cookie::SameSitePolicy::Strict:
        return Inspector::Protocol::Playwright::CookieSameSitePolicy::Strict;
    }
    ASSERT_NOT_REACHED();
    return Inspector::Protocol::Playwright::CookieSameSitePolicy::None;
}

Ref<Inspector::Protocol::Playwright::Cookie> buildObjectForCookie(const WebCore::Cookie& cookie)
{
    return Inspector::Protocol::Playwright::Cookie::create()
        .setName(cookie.name)
        .setValue(cookie.value)
        .setDomain(cookie.domain)
        .setPath(cookie.path)
        .setExpires(cookie.expires.value_or(-1))
        .setHttpOnly(cookie.httpOnly)
        .setSecure(cookie.secure)
        .setSession(cookie.session)
        .setSameSite(cookieSameSitePolicy(cookie.sameSite))
        .release();
}

}  // namespace

BrowserContext::BrowserContext() = default;

BrowserContext::~BrowserContext() = default;

class InspectorPlaywrightAgent::BrowserContextDeletion {
    WTF_MAKE_NONCOPYABLE(BrowserContextDeletion);
    WTF_MAKE_FAST_ALLOCATED;
public:
    BrowserContextDeletion(std::unique_ptr<BrowserContext>&& context, size_t numberOfPages, Ref<DeleteContextCallback>&& callback)
        : m_browserContext(WTFMove(context))
        , m_numberOfPages(numberOfPages)
        , m_callback(WTFMove(callback)) { }

    void didDestroyPage(const WebPageProxy& page)
    {
        ASSERT(m_browserContext->dataStore->sessionID() == page.sessionID());
        // Check if new pages have been created during the context destruction and
        // close all of them if necessary.
        if (m_numberOfPages == 1) {
            auto pages = m_browserContext->pages;
            size_t numberOfPages = pages.size();
            if (numberOfPages > 1) {
                m_numberOfPages = numberOfPages;
                for (auto* existingPage : pages) {
                    if (existingPage != &page)
                        existingPage->closePage();
                }
            }
        }
        --m_numberOfPages;
        if (m_numberOfPages)
            return;
        m_callback->sendSuccess();
    }

    bool isFinished() const { return !m_numberOfPages; }

    BrowserContext* context() const { return m_browserContext.get(); }

private:
    std::unique_ptr<BrowserContext> m_browserContext;
    size_t m_numberOfPages;
    Ref<DeleteContextCallback> m_callback;
};


InspectorPlaywrightAgent::InspectorPlaywrightAgent(std::unique_ptr<InspectorPlaywrightAgentClient> client)
    : m_frontendChannel(nullptr)
    , m_frontendRouter(FrontendRouter::create())
    , m_backendDispatcher(BackendDispatcher::create(m_frontendRouter.copyRef()))
    , m_client(std::move(client))
    , m_frontendDispatcher(makeUnique<PlaywrightFrontendDispatcher>(m_frontendRouter))
    , m_playwrightDispatcher(PlaywrightBackendDispatcher::create(m_backendDispatcher.get(), this))
{
}

InspectorPlaywrightAgent::~InspectorPlaywrightAgent()
{
    if (m_frontendChannel)
        disconnectFrontend();
}

void InspectorPlaywrightAgent::connectFrontend(FrontendChannel& frontendChannel)
{
    ASSERT(!m_frontendChannel);
    m_frontendChannel = &frontendChannel;
    WebPageInspectorController::setObserver(this);

    m_frontendRouter->connectFrontend(frontendChannel);
}

void InspectorPlaywrightAgent::disconnectFrontend()
{
    if (!m_frontendChannel)
        return;

    disable();

    m_frontendRouter->disconnectFrontend(*m_frontendChannel);
    ASSERT(!m_frontendRouter->hasFrontends());

    WebPageInspectorController::setObserver(nullptr);
    m_frontendChannel = nullptr;

    closeImpl([](String error){});
}

void InspectorPlaywrightAgent::dispatchMessageFromFrontend(const String& message)
{
    m_backendDispatcher->dispatch(message, [&](const RefPtr<JSON::Object>& messageObject) {
        RefPtr<JSON::Value> idValue;
        if (!messageObject->getValue("id"_s, idValue))
            return BackendDispatcher::InterceptionResult::Continue;
        RefPtr<JSON::Value> pageProxyIDValue;
        if (!messageObject->getValue("pageProxyId"_s, pageProxyIDValue))
            return BackendDispatcher::InterceptionResult::Continue;

        String pageProxyID;
        if (!pageProxyIDValue->asString(pageProxyID)) {
            m_backendDispatcher->reportProtocolError(BackendDispatcher::InvalidRequest, "The type of 'pageProxyId' must be string"_s);
            m_backendDispatcher->sendPendingErrors();
            return BackendDispatcher::InterceptionResult::Intercepted;
        }

        if (auto pageProxyChannel = m_pageProxyChannels.get(pageProxyID)) {
            pageProxyChannel->dispatchMessageFromFrontend(message);
            return BackendDispatcher::InterceptionResult::Intercepted;
        }

        std::optional<int> requestId = idValue->asInteger();
        if (!requestId) {
            m_backendDispatcher->reportProtocolError(BackendDispatcher::InvalidRequest, "The type of 'id' must be number"_s);
            m_backendDispatcher->sendPendingErrors();
            return BackendDispatcher::InterceptionResult::Intercepted;
        }

        m_backendDispatcher->reportProtocolError(*requestId, BackendDispatcher::InvalidParams, "Cannot find page proxy with provided 'pageProxyId'"_s);
        m_backendDispatcher->sendPendingErrors();
        return BackendDispatcher::InterceptionResult::Intercepted;
    });
}

void InspectorPlaywrightAgent::didCreateInspectorController(WebPageProxy& page)
{
    if (!m_isEnabled)
        return;

    if (isInspectorProcessPool(page.process().processPool()))
        return;

    ASSERT(m_frontendChannel);

    String browserContextID = toBrowserContextIDProtocolString(page.sessionID());
    String pageProxyID = toPageProxyIDProtocolString(page);
    auto* opener = page.configuration().relatedPage();
    String openerId;
    if (opener)
        openerId = toPageProxyIDProtocolString(*opener);

    BrowserContext* browserContext = getExistingBrowserContext(browserContextID);
    browserContext->pages.add(&page);
    m_frontendDispatcher->pageProxyCreated(browserContextID, pageProxyID, openerId);

    // Auto-connect to all new pages.
    auto pageProxyChannel = makeUnique<PageProxyChannel>(*m_frontendChannel, browserContextID, pageProxyID, page);
    page.inspectorController().connectFrontend(*pageProxyChannel);
    // Always pause new targets if controlled remotely.
    page.inspectorController().setPauseOnStart(true);
    m_pageProxyChannels.set(pageProxyID, WTFMove(pageProxyChannel));
    page.setFullScreenManagerClientOverride(makeUnique<PlaywrightFullScreenManagerProxyClient>(page));
}

void InspectorPlaywrightAgent::willDestroyInspectorController(WebPageProxy& page)
{
    if (!m_isEnabled)
        return;

    if (isInspectorProcessPool(page.process().processPool()))
        return;

    String browserContextID = toBrowserContextIDProtocolString(page.sessionID());
    BrowserContext* browserContext = getExistingBrowserContext(browserContextID);
    browserContext->pages.remove(&page);
    m_frontendDispatcher->pageProxyDestroyed(toPageProxyIDProtocolString(page));

    auto it = m_browserContextDeletions.find(browserContextID);
    if (it != m_browserContextDeletions.end()) {
        it->value->didDestroyPage(page);
        if (it->value->isFinished())
            m_browserContextDeletions.remove(it);
    }

    String pageProxyID = toPageProxyIDProtocolString(page);
    auto channelIt = m_pageProxyChannels.find(pageProxyID);
    ASSERT(channelIt != m_pageProxyChannels.end());
    channelIt->value->disconnect();
    m_pageProxyChannels.remove(channelIt);
}

void InspectorPlaywrightAgent::didFailProvisionalLoad(WebPageProxy& page, uint64_t navigationID, const String& error)
{
    if (!m_isEnabled)
        return;

    m_frontendDispatcher->provisionalLoadFailed(
        toPageProxyIDProtocolString(page),
        String::number(navigationID), error);
}

void InspectorPlaywrightAgent::willCreateNewPage(WebPageProxy& page, const WebCore::WindowFeatures& features, const URL& url)
{
    if (!m_isEnabled)
        return;

    m_frontendDispatcher->windowOpen(
        toPageProxyIDProtocolString(page),
        url.string(),
        getEnabledWindowFeatures(features));
}

void InspectorPlaywrightAgent::didFinishScreencast(const PAL::SessionID& sessionID, const String& screencastID)
{
    if (!m_isEnabled)
        return;

    m_frontendDispatcher->screencastFinished(screencastID);
}

static WebsiteDataStore* findDefaultWebsiteDataStore() {
    WebsiteDataStore* result = nullptr;
    WebsiteDataStore::forEachWebsiteDataStore([&result] (WebsiteDataStore& dataStore) {
        if (dataStore.isPersistent()) {
            RELEASE_ASSERT(result == nullptr);
            result = &dataStore;
        }
    });
    return result;
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::enable()
{
    if (m_isEnabled)
        return { };

    m_isEnabled = true;

    auto* defaultDataStore = findDefaultWebsiteDataStore();
    if (!m_defaultContext && defaultDataStore) {
        auto context = std::make_unique<BrowserContext>();
        m_defaultContext = context.get();
        context->processPool = WebProcessPool::allProcessPools().first().ptr();
        context->dataStore = defaultDataStore;
        setGeolocationProvider(context.get());
        // Add default context to the map so that we can easily find it for
        // created/deleted pages.
        PAL::SessionID sessionID = context->dataStore->sessionID();
        m_browserContexts.set(toBrowserContextIDProtocolString(sessionID), WTFMove(context));
    }

    WebsiteDataStore::forEachWebsiteDataStore([this] (WebsiteDataStore& dataStore) {
        dataStore.setDownloadInstrumentation(this);
    });
    for (Ref pool : WebProcessPool::allProcessPools()) {
        for (Ref process : pool->processes()) {
            for (Ref page : process->pages())
                didCreateInspectorController(WTFMove(page));
        }
    }
    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::disable()
{
    if (!m_isEnabled)
        return { };

    m_isEnabled = false;

    for (auto it = m_pageProxyChannels.begin(); it != m_pageProxyChannels.end(); ++it)
        it->value->disconnect();
    m_pageProxyChannels.clear();

    WebsiteDataStore::forEachWebsiteDataStore([] (WebsiteDataStore& dataStore) {
        dataStore.setDownloadInstrumentation(nullptr);
        dataStore.setDownloadForAutomation(std::optional<bool>(), String());
    });
    for (auto& it : m_browserContexts) {
        it.value->dataStore->setDownloadInstrumentation(nullptr);
        it.value->pages.clear();
    }
    m_browserContextDeletions.clear();
    return { };
}

Inspector::Protocol::ErrorStringOr<String> InspectorPlaywrightAgent::getInfo()
{
#if PLATFORM(MAC)
    return { "macOS"_s };
#elif PLATFORM(GTK) || PLATFORM(WPE)
    return { "Linux"_s };
#elif PLATFORM(WIN)
    return { "Windows"_s };
#else
#error "Unsupported platform."
#endif
}

void InspectorPlaywrightAgent::close(Ref<CloseCallback>&& callback)
{
    closeImpl([callback = WTFMove(callback)] (String error) {
        if (!callback->isActive())
            return;
        if (error.isNull())
            callback->sendSuccess();
        else
            callback->sendFailure(error);
    });
}

void InspectorPlaywrightAgent::closeImpl(Function<void(String)>&& callback)
{
    Vector<Ref<WebPageProxy>> pages;
    // If Web Process crashed it will be disconnected from its pool until
    // the page reloads. So we cannot discover such processes and the pages
    // by traversing all process pools and their processes. Instead we look at
    // all existing Web Processes wether in a pool or not.
    for (Ref process : WebProcessProxy::allProcessesForInspector()) {
        for (Ref page : process->pages())
            pages.append(WTFMove(page));
    }
    for (Ref page : pages)
        page->closePage();

    if (!m_defaultContext) {
        m_client->closeBrowser();
        callback(String());
        return;
    }

    m_defaultContext->dataStore->syncLocalStorage([this, callback = WTFMove(callback)] () {
        if (m_client == nullptr) {
            callback("no platform delegate to close browser"_s);
        } else {
            m_client->closeBrowser();
            callback(String());
        }
    });

}

Inspector::Protocol::ErrorStringOr<String /* browserContextID */> InspectorPlaywrightAgent::createContext(const String& proxyServer, const String& proxyBypassList)
{
    String errorString;
    std::unique_ptr<BrowserContext> browserContext = m_client->createBrowserContext(errorString, proxyServer, proxyBypassList);
    if (!browserContext)
        return makeUnexpected(errorString);

    // Ensure network process.
    browserContext->dataStore->networkProcess();
    browserContext->dataStore->setDownloadInstrumentation(this);
    setGeolocationProvider(browserContext.get());
    PAL::SessionID sessionID = browserContext->dataStore->sessionID();
    String browserContextID = toBrowserContextIDProtocolString(sessionID);
    m_browserContexts.set(browserContextID, WTFMove(browserContext));
    return browserContextID;
}

void InspectorPlaywrightAgent::deleteContext(const String& browserContextID, Ref<DeleteContextCallback>&& callback)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!lookupBrowserContext(errorString, browserContextID)) {
        callback->sendFailure(errorString);
        return;
    }

    if (browserContext == m_defaultContext) {
        callback->sendFailure("Cannot delete default context"_s);
        return;
    }

    auto pages = browserContext->pages;
    PAL::SessionID sessionID = browserContext->dataStore->sessionID();
    auto contextHolder = m_browserContexts.take(browserContextID);
    if (pages.isEmpty()) {
        callback->sendSuccess();
    } else {
        m_browserContextDeletions.set(browserContextID, makeUnique<BrowserContextDeletion>(WTFMove(contextHolder), pages.size(), WTFMove(callback)));
        for (auto* page : pages)
            page->closePage();
    }
    m_client->deleteBrowserContext(errorString, sessionID);
}

Inspector::Protocol::ErrorStringOr<String /* pageProxyID */> InspectorPlaywrightAgent::createPage(const String& browserContextID)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!browserContext)
        return makeUnexpected(errorString);

    RefPtr<WebPageProxy> page = m_client->createPage(errorString, *browserContext);
    if (!page)
        return makeUnexpected(errorString);

    return toPageProxyIDProtocolString(*page);
}

WebFrameProxy* InspectorPlaywrightAgent::frameForID(const String& frameID, String& error)
{
    size_t dotPos = frameID.find("."_s);
    if (dotPos == notFound) {
        error = "Invalid frame id"_s;
        return nullptr;
    }

    if (!frameID.containsOnlyASCII()) {
        error = "Invalid frame id"_s;
        return nullptr;
    }

    String processIDString = frameID.left(dotPos);
    uint64_t pid = strtoull(processIDString.ascii().data(), 0, 10);
    auto processID = ObjectIdentifier<WebCore::ProcessIdentifierType>(pid);
    auto process = WebProcessProxy::processForIdentifier(processID);
    if (!process) {
        error = "Cannot find web process for the frame id"_s;
        return nullptr;
    }

    String frameIDString = frameID.substring(dotPos + 1);
    uint64_t frameIDNumber = strtoull(frameIDString.ascii().data(), 0, 10);
    auto frameIdentifier = WebCore::FrameIdentifier {
       ObjectIdentifier<WebCore::FrameIdentifierType>(frameIDNumber),
       processID
    };
    WebFrameProxy* frame = WebFrameProxy::webFrame(frameIdentifier);
    if (!frame) {
        error = "Cannot find web frame for the frame id"_s;
        return nullptr;
    }

    return frame;
}

void InspectorPlaywrightAgent::navigate(const String& url, const String& pageProxyID, const String& frameID, const String& referrer, Ref<NavigateCallback>&& callback)
{
    auto* pageProxyChannel = m_pageProxyChannels.get(pageProxyID);
    if (!pageProxyChannel) {
        callback->sendFailure("Cannot find page proxy with provided 'pageProxyId'"_s);
        return;
    }

    WebCore::ResourceRequest resourceRequest { url };

    if (!!referrer)
        resourceRequest.setHTTPReferrer(referrer);

    if (!resourceRequest.url().isValid()) {
        callback->sendFailure("Cannot navigate to invalid URL"_s);
        return;
    }

    WebFrameProxy* frame = nullptr;
    if (!!frameID) {
        String error;
        frame = frameForID(frameID, error);
        if (!frame) {
            callback->sendFailure(error);
            return;
        }

        if (frame->page() != &pageProxyChannel->page()) {
            callback->sendFailure("Frame with specified is not from the specified page"_s);
            return;
        }
    }

    pageProxyChannel->page().inspectorController().navigate(WTFMove(resourceRequest), frame, [callback = WTFMove(callback)](const String& error, uint64_t navigationID) {
        if (!error.isEmpty()) {
            callback->sendFailure(error);
            return;
        }

        String navigationIDString;
        if (navigationID)
            navigationIDString = String::number(navigationID);
        callback->sendSuccess(navigationIDString);
    });
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::grantFileReadAccess(const String& pageProxyID, Ref<JSON::Array>&& paths)
{
#if ENABLE(SANDBOX_EXTENSIONS)
    auto* pageProxyChannel = m_pageProxyChannels.get(pageProxyID);
    if (!pageProxyChannel)
        return makeUnexpected("Unknown pageProxyID"_s);

    Vector<String> files;
    for (const auto& value : paths.get()) {
        String path;
        if (!value->asString(path))
            return makeUnexpected("Filr path must be a string"_s);

        files.append(path);
    }

    auto sandboxExtensionHandles = SandboxExtension::createReadOnlyHandlesForFiles("InspectorPlaywrightAgent::grantFileReadAccess"_s, files);
    pageProxyChannel->page().send(Messages::WebPage::ExtendSandboxForFilesFromOpenPanel(WTFMove(sandboxExtensionHandles)));
#endif
    return { };
}

void InspectorPlaywrightAgent::takePageScreenshot(const String& pageProxyID, int x, int y, int width, int height, std::optional<bool>&& omitDeviceScaleFactor, Ref<TakePageScreenshotCallback>&& callback)
{
#if PLATFORM(MAC) || PLATFORM(GTK) || PLATFORM(WPE)
    auto* pageProxyChannel = m_pageProxyChannels.get(pageProxyID);
    if (!pageProxyChannel) {
        callback->sendFailure("Unknown pageProxyID"_s);
        return;
    }

    bool nominalResolution = omitDeviceScaleFactor.has_value() && *omitDeviceScaleFactor;
    WebCore::IntRect clip(x, y, width, height);
    m_client->takePageScreenshot(pageProxyChannel->page(), WTFMove(clip), nominalResolution, [callback = WTFMove(callback)](const String& error, const String& data) {
        if (error.isEmpty())
            callback->sendSuccess(data);
        else
            callback->sendFailure(error);
    });
#else
    return callback->sendFailure("This method is not supported on this platform."_s);
#endif
}


Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::setIgnoreCertificateErrors(const String& browserContextID, bool ignore)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty())
        return makeUnexpected(errorString);

    PAL::SessionID sessionID = browserContext->dataStore->sessionID();
    NetworkProcessProxy& networkProcess = browserContext->dataStore->networkProcess();
    networkProcess.send(Messages::NetworkProcess::SetIgnoreCertificateErrors(sessionID, ignore), 0);
    return { };
}

void InspectorPlaywrightAgent::getAllCookies(const String& browserContextID, Ref<GetAllCookiesCallback>&& callback) {
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty()) {
        callback->sendFailure(errorString);
        return;
    }

    browserContext->dataStore->cookieStore().cookies(
        [callback = WTFMove(callback)](const Vector<WebCore::Cookie>& allCookies) {
            if (!callback->isActive())
                return;
            auto cookies = JSON::ArrayOf<Inspector::Protocol::Playwright::Cookie>::create();

// Soup returns cookies in the reverse order.
#if USE(SOUP)
            for (const auto& cookie : makeReversedRange(allCookies))
#else
            for (const auto& cookie : allCookies)
#endif
                cookies->addItem(buildObjectForCookie(cookie));
            callback->sendSuccess(WTFMove(cookies));
        });
}

void InspectorPlaywrightAgent::setCookies(const String& browserContextID, Ref<JSON::Array>&& in_cookies, Ref<SetCookiesCallback>&& callback) {
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty()) {
        callback->sendFailure(errorString);
        return;
    }

    Vector<WebCore::Cookie> cookies;
    for (unsigned i = 0; i < in_cookies->length(); ++i) {
        RefPtr<JSON::Value> item = in_cookies->get(i);
        RefPtr<JSON::Object> obj = item->asObject();
        if (!obj) {
            callback->sendFailure("Invalid cookie payload format"_s);
            return;
        }

        WebCore::Cookie cookie;
        cookie.name = obj->getString("name"_s);
        cookie.value = obj->getString("value"_s);
        cookie.domain = obj->getString("domain"_s);
        cookie.path = obj->getString("path"_s);
        if (!cookie.name || !cookie.value || !cookie.domain || !cookie.path) {
            callback->sendFailure("Invalid file payload format"_s);
            return;
        }

        std::optional<double> expires = obj->getDouble("expires"_s);
        if (expires && *expires != -1)
            cookie.expires = *expires;
        if (std::optional<bool> value = obj->getBoolean("httpOnly"_s))
            cookie.httpOnly = *value;
        if (std::optional<bool> value = obj->getBoolean("secure"_s))
            cookie.secure = *value;
        if (std::optional<bool> value = obj->getBoolean("session"_s))
            cookie.session = *value;
        String sameSite;
        if (obj->getString("sameSite"_s, sameSite)) {
            if (sameSite == "None"_s)
                cookie.sameSite = WebCore::Cookie::SameSitePolicy::None;
            if (sameSite == "Lax"_s)
                cookie.sameSite = WebCore::Cookie::SameSitePolicy::Lax;
            if (sameSite == "Strict"_s)
                cookie.sameSite = WebCore::Cookie::SameSitePolicy::Strict;
        }
        cookies.append(WTFMove(cookie));
    }

    browserContext->dataStore->cookieStore().setCookies(WTFMove(cookies),
        [callback = WTFMove(callback)]() {
            if (!callback->isActive())
                return;
            callback->sendSuccess();
        });
}

void InspectorPlaywrightAgent::deleteAllCookies(const String& browserContextID, Ref<DeleteAllCookiesCallback>&& callback) {
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty()) {
        callback->sendFailure(errorString);
        return;
    }

    browserContext->dataStore->cookieStore().deleteAllCookies(
        [callback = WTFMove(callback)]() {
            if (!callback->isActive())
                return;
            callback->sendSuccess();
        });
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::setLanguages(Ref<JSON::Array>&& languages, const String& browserContextID)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty())
        return makeUnexpected(errorString);

    Vector<String> items;
    for (const auto& value : languages.get()) {
        String language;
        if (!value->asString(language))
            return makeUnexpected("Language must be a string"_s);

        items.append(language);
    }

    browserContext->processPool->configuration().setOverrideLanguages(WTFMove(items));
    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::setDownloadBehavior(const String& behavior, const String& downloadPath, const String& browserContextID)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty())
        return makeUnexpected(errorString);

    std::optional<bool> allow;
    if (behavior == "allow"_s)
      allow = true;
    if (behavior == "deny"_s)
      allow = false;
    browserContext->dataStore->setDownloadForAutomation(allow, downloadPath);
    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::setGeolocationOverride(const String& browserContextID, RefPtr<JSON::Object>&& geolocation)
{
    String errorString;
    BrowserContext* browserContext = lookupBrowserContext(errorString, browserContextID);
    if (!errorString.isEmpty())
        return makeUnexpected(errorString);

    auto* geoManager = browserContext->processPool->supplement<WebGeolocationManagerProxy>();
    if (!geoManager)
        return makeUnexpected("Internal error: geolocation manager is not available."_s);

    if (geolocation) {
        std::optional<double> timestamp = geolocation->getDouble("timestamp"_s);
        std::optional<double> latitude = geolocation->getDouble("latitude"_s);
        std::optional<double> longitude = geolocation->getDouble("longitude"_s);
        std::optional<double> accuracy = geolocation->getDouble("accuracy"_s);
        if (!timestamp || !latitude || !longitude || !accuracy)
            return makeUnexpected("Invalid geolocation format"_s);

        auto position = WebGeolocationPosition::create(WebCore::GeolocationPositionData(*timestamp, *latitude, *longitude, *accuracy));
        if (!browserContext->geolocationProvider)
            return makeUnexpected("Internal error: geolocation provider has been destroyed."_s);
        browserContext->geolocationProvider->setPosition(position);
        geoManager->providerDidChangePosition(&position.get());
    } else {
        geoManager->providerDidFailToDeterminePosition("Position unavailable"_s);
    }
    return { };
}

void InspectorPlaywrightAgent::downloadCreated(const String& uuid, const WebCore::ResourceRequest& request, const FrameInfoData& frameInfoData, WebPageProxy* page, RefPtr<DownloadProxy> download)
{
    if (!m_isEnabled)
        return;
    String frameID = WebCore::InspectorPageAgent::makeFrameID(page->process().coreProcessIdentifier(), frameInfoData.frameID);
    m_downloads.set(uuid, download);
    m_frontendDispatcher->downloadCreated(
        toPageProxyIDProtocolString(*page),
        frameID,
        uuid, request.url().string());
}

void InspectorPlaywrightAgent::downloadFilenameSuggested(const String& uuid, const String& suggestedFilename)
{
    if (!m_isEnabled)
        return;
    m_frontendDispatcher->downloadFilenameSuggested(uuid, suggestedFilename);
}

void InspectorPlaywrightAgent::downloadFinished(const String& uuid, const String& error)
{
    if (!m_isEnabled)
        return;
    m_frontendDispatcher->downloadFinished(uuid, error);
    m_downloads.remove(uuid);
}

Inspector::Protocol::ErrorStringOr<void> InspectorPlaywrightAgent::cancelDownload(const String& uuid)
{
    if (!m_isEnabled)
        return { };
    auto download = m_downloads.get(uuid);
    if (!download)
        return { };
    download->cancel([] (auto*) {});
    return { };
}

void InspectorPlaywrightAgent::clearMemoryCache(const String& browserContextID, Ref<ClearMemoryCacheCallback>&& callback)
{
    if (!m_isEnabled) {
        callback->sendSuccess();
        return;
    }
    auto browserContext = getExistingBrowserContext(browserContextID);
    if (!browserContext) {
        callback->sendSuccess();
        return;
    }
    browserContext->dataStore->removeData(WebKit::WebsiteDataType::MemoryCache, -WallTime::infinity(), [callback] {
        callback->sendSuccess();
    });
}

BrowserContext* InspectorPlaywrightAgent::getExistingBrowserContext(const String& browserContextID)
{
    BrowserContext* browserContext = m_browserContexts.get(browserContextID);
    if (browserContext)
        return browserContext;

    auto it = m_browserContextDeletions.find(browserContextID);
    RELEASE_ASSERT(it != m_browserContextDeletions.end());
    return it->value->context();
}

BrowserContext* InspectorPlaywrightAgent::lookupBrowserContext(ErrorString& errorString, const String& browserContextID)
{
    if (!browserContextID) {
        if (!m_defaultContext)
            errorString = "Browser started with no default context"_s;
        return m_defaultContext;
    }

    BrowserContext* browserContext = m_browserContexts.get(browserContextID);
    if (!browserContext)
        errorString = "Could not find browser context for given id"_s;
    return browserContext;
}

} // namespace WebKit

#endif // ENABLE(REMOTE_INSPECTOR)
