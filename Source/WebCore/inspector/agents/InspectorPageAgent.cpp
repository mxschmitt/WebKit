/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2015-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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
#include "InspectorPageAgent.h"

#include "AXObjectCache.h"
#include "BackForwardController.h"
#include "CachedResource.h"
#include "CachedResourceLoader.h"
#include "Cookie.h"
#include "CookieJar.h"
#include "CustomHeaderFields.h"
#include "DOMWrapperWorld.h"
#include "DocumentInlines.h"
#include "DocumentLoader.h"
#include "Editor.h"
#include "ElementInlines.h"
#include "FocusController.h"
#include "ForcedAccessibilityValue.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "FrameSnapshotting.h"
#include "HTMLFrameOwnerElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "ImageBuffer.h"
#include "InspectorClient.h"
#include "InspectorDOMAgent.h"
#include "InspectorNetworkAgent.h"
#include "InspectorOverlay.h"
#include "InstrumentingAgents.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "MIMETypeRegistry.h"
#include "Page.h"
#include "PageRuntimeAgent.h"
#include "PlatformScreen.h"
#include "RenderObject.h"
#include "RenderTheme.h"
#include "DeprecatedGlobalSettings.h"
#include "SimpleRange.h"
#include "ScriptController.h"
#include "ScriptSourceCode.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include "StyleScope.h"
#include "Theme.h"
#include <pal/text/TextEncoding.h>
#include "TextIterator.h"
#include "TypingCommand.h"
#include "UserGestureIndicator.h"
#include <JavaScriptCore/ContentSearchUtilities.h>
#include <JavaScriptCore/IdentifiersFactory.h>
#include <JavaScriptCore/InjectedScriptManager.h>
#include <JavaScriptCore/RegularExpression.h>
#include <wtf/DateMath.h>
#include <wtf/ListHashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/Stopwatch.h>
#include <wtf/text/Base64.h>
#include <wtf/text/StringBuilder.h>

#if ENABLE(APPLICATION_MANIFEST)
#include "CachedApplicationManifest.h"
#endif

#if ENABLE(WEB_ARCHIVE) && USE(CF)
#include "LegacyWebArchive.h"
#endif

namespace WebCore {

using namespace Inspector;

static HashMap<String, Ref<DOMWrapperWorld>>& createdUserWorlds() {
    static NeverDestroyed<HashMap<String, Ref<DOMWrapperWorld>>> nameToWorld;
    return nameToWorld;
}

static bool decodeBuffer(std::span<const uint8_t> buffer, const String& textEncodingName, String* result)
{
    if (buffer.data()) {
        PAL::TextEncoding encoding(textEncodingName);
        if (!encoding.isValid())
            encoding = PAL::WindowsLatin1Encoding();
        *result = encoding.decode(buffer);
        return true;
    }
    return false;
}

bool InspectorPageAgent::mainResourceContent(LocalFrame* frame, bool withBase64Encode, String* result)
{
    RefPtr<FragmentedSharedBuffer> buffer = frame->loader().documentLoader()->mainResourceData();
    if (!buffer)
        return false;
    return InspectorPageAgent::dataContent(buffer->makeContiguous()->span(), frame->document()->encoding(), withBase64Encode, result);
}

bool InspectorPageAgent::sharedBufferContent(RefPtr<FragmentedSharedBuffer>&& buffer, const String& textEncodingName, bool withBase64Encode, String* result)
{
    return dataContent(buffer ? buffer->makeContiguous()->span() : std::span<const uint8_t> { }, textEncodingName, withBase64Encode, result);
}

bool InspectorPageAgent::dataContent(std::span<const uint8_t> data, const String& textEncodingName, bool withBase64Encode, String* result)
{
    if (withBase64Encode) {
        *result = base64EncodeToString(data);
        return true;
    }

    return decodeBuffer(data, textEncodingName, result);
}

Vector<CachedResource*> InspectorPageAgent::cachedResourcesForFrame(LocalFrame* frame)
{
    Vector<CachedResource*> result;

    for (auto& cachedResourceHandle : frame->document()->cachedResourceLoader().allCachedResources().values()) {
        auto* cachedResource = cachedResourceHandle.get();
        if (cachedResource->resourceRequest().hiddenFromInspector())
            continue;

        switch (cachedResource->type()) {
        case CachedResource::Type::ImageResource:
            // Skip images that were not auto loaded (images disabled in the user agent).
        case CachedResource::Type::SVGFontResource:
        case CachedResource::Type::FontResource:
            // Skip fonts that were referenced in CSS but never used/downloaded.
            if (cachedResource->stillNeedsLoad())
                continue;
            break;
        default:
            // All other CachedResource types download immediately.
            break;
        }

        result.append(cachedResource);
    }

    return result;
}

void InspectorPageAgent::resourceContent(Inspector::Protocol::ErrorString& errorString, LocalFrame* frame, const URL& url, String* result, bool* base64Encoded)
{
    DocumentLoader* loader = assertDocumentLoader(errorString, frame);
    if (!loader)
        return;

    RefPtr<FragmentedSharedBuffer> buffer;
    bool success = false;
    if (equalIgnoringFragmentIdentifier(url, loader->url())) {
        *base64Encoded = false;
        success = mainResourceContent(frame, *base64Encoded, result);
    }

    if (!success) {
        if (auto* resource = cachedResource(frame, url))
            success = InspectorNetworkAgent::cachedResourceContent(*resource, result, base64Encoded);
    }

    if (!success)
        errorString = "Missing resource for given url"_s;
}

String InspectorPageAgent::sourceMapURLForResource(CachedResource* cachedResource)
{
    if (!cachedResource)
        return String();

    // Scripts are handled in a separate path.
    if (cachedResource->type() != CachedResource::Type::CSSStyleSheet)
        return String();

    String sourceMapHeader = cachedResource->response().httpHeaderField(HTTPHeaderName::SourceMap);
    if (!sourceMapHeader.isEmpty())
        return sourceMapHeader;

    sourceMapHeader = cachedResource->response().httpHeaderField(HTTPHeaderName::XSourceMap);
    if (!sourceMapHeader.isEmpty())
        return sourceMapHeader;

    String content;
    bool base64Encoded;
    if (InspectorNetworkAgent::cachedResourceContent(*cachedResource, &content, &base64Encoded) && !base64Encoded)
        return ContentSearchUtilities::findStylesheetSourceMapURL(content);

    return String();
}

CachedResource* InspectorPageAgent::cachedResource(const LocalFrame* frame, const URL& url)
{
    if (url.isNull())
        return nullptr;

    CachedResource* cachedResource = frame->document()->cachedResourceLoader().cachedResource(MemoryCache::removeFragmentIdentifierIfNeeded(url));
    if (!cachedResource) {
        ResourceRequest request(url);
        request.setDomainForCachePartition(frame->document()->domainForCachePartition());
        cachedResource = MemoryCache::singleton().resourceForRequest(request, frame->page()->sessionID());
    }

    return cachedResource;
}

Inspector::Protocol::Page::ResourceType InspectorPageAgent::resourceTypeJSON(InspectorPageAgent::ResourceType resourceType)
{
    switch (resourceType) {
    case DocumentResource:
        return Inspector::Protocol::Page::ResourceType::Document;
    case ImageResource:
        return Inspector::Protocol::Page::ResourceType::Image;
    case FontResource:
        return Inspector::Protocol::Page::ResourceType::Font;
    case StyleSheetResource:
        return Inspector::Protocol::Page::ResourceType::StyleSheet;
    case ScriptResource:
        return Inspector::Protocol::Page::ResourceType::Script;
    case XHRResource:
        return Inspector::Protocol::Page::ResourceType::XHR;
    case FetchResource:
        return Inspector::Protocol::Page::ResourceType::Fetch;
    case PingResource:
        return Inspector::Protocol::Page::ResourceType::Ping;
    case BeaconResource:
        return Inspector::Protocol::Page::ResourceType::Beacon;
    case WebSocketResource:
        return Inspector::Protocol::Page::ResourceType::WebSocket;
    case EventSourceResource:
        return Inspector::Protocol::Page::ResourceType::EventSource;
    case OtherResource:
        return Inspector::Protocol::Page::ResourceType::Other;
#if ENABLE(APPLICATION_MANIFEST)
    case ApplicationManifestResource:
        break;
#endif
    }
    return Inspector::Protocol::Page::ResourceType::Other;
}

InspectorPageAgent::ResourceType InspectorPageAgent::inspectorResourceType(CachedResource::Type type)
{
    switch (type) {
    case CachedResource::Type::ImageResource:
        return InspectorPageAgent::ImageResource;
    case CachedResource::Type::SVGFontResource:
    case CachedResource::Type::FontResource:
        return InspectorPageAgent::FontResource;
#if ENABLE(XSLT)
    case CachedResource::Type::XSLStyleSheet:
#endif
    case CachedResource::Type::CSSStyleSheet:
        return InspectorPageAgent::StyleSheetResource;
    case CachedResource::Type::Script:
        return InspectorPageAgent::ScriptResource;
    case CachedResource::Type::MainResource:
        return InspectorPageAgent::DocumentResource;
    case CachedResource::Type::Beacon:
        return InspectorPageAgent::BeaconResource;
#if ENABLE(APPLICATION_MANIFEST)
    case CachedResource::Type::ApplicationManifest:
        return InspectorPageAgent::ApplicationManifestResource;
#endif
    case CachedResource::Type::Ping:
        return InspectorPageAgent::PingResource;
    case CachedResource::Type::MediaResource:
    case CachedResource::Type::Icon:
    case CachedResource::Type::RawResource:
    default:
        return InspectorPageAgent::OtherResource;
    }
}

InspectorPageAgent::ResourceType InspectorPageAgent::inspectorResourceType(const CachedResource& cachedResource)
{
    if (cachedResource.type() == CachedResource::Type::MainResource && MIMETypeRegistry::isSupportedImageMIMEType(cachedResource.mimeType()))
        return InspectorPageAgent::ImageResource;

    if (cachedResource.type() == CachedResource::Type::RawResource) {
        switch (cachedResource.resourceRequest().requester()) {
        case ResourceRequestRequester::Fetch:
            return InspectorPageAgent::FetchResource;
        case ResourceRequestRequester::Main:
            return InspectorPageAgent::DocumentResource;
        case ResourceRequestRequester::EventSource:
            return InspectorPageAgent::EventSourceResource;
        default:
            return InspectorPageAgent::XHRResource;
        }
    }

    return inspectorResourceType(cachedResource.type());
}

Inspector::Protocol::Page::ResourceType InspectorPageAgent::cachedResourceTypeJSON(const CachedResource& cachedResource)
{
    return resourceTypeJSON(inspectorResourceType(cachedResource));
}

LocalFrame* InspectorPageAgent::findFrameWithSecurityOrigin(Page& page, const String& originRawString)
{
    for (Frame* frame = &page.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        if (localFrame->document()->securityOrigin().toRawString() == originRawString)
            return localFrame;
    }
    return nullptr;
}

DocumentLoader* InspectorPageAgent::assertDocumentLoader(Inspector::Protocol::ErrorString& errorString, LocalFrame* frame)
{
    FrameLoader& frameLoader = frame->loader();
    DocumentLoader* documentLoader = frameLoader.documentLoader();
    if (!documentLoader)
        errorString = "Missing document loader for given frame"_s;
    return documentLoader;
}

InspectorPageAgent::InspectorPageAgent(PageAgentContext& context, InspectorClient* client, InspectorOverlay* overlay)
    : InspectorAgentBase("Page"_s, context)
    , m_frontendDispatcher(makeUnique<Inspector::PageFrontendDispatcher>(context.frontendRouter))
    , m_backendDispatcher(Inspector::PageBackendDispatcher::create(context.backendDispatcher, this))
    , m_inspectedPage(context.inspectedPage)
    , m_injectedScriptManager(context.injectedScriptManager)
    , m_client(client)
    , m_overlay(overlay)
{
}

InspectorPageAgent::~InspectorPageAgent() = default;

void InspectorPageAgent::didCreateFrontendAndBackend(Inspector::FrontendRouter*, Inspector::BackendDispatcher*)
{
}

void InspectorPageAgent::willDestroyFrontendAndBackend(Inspector::DisconnectReason)
{
    disable();
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::enable()
{
    if (m_instrumentingAgents.enabledPageAgent() == this)
        return makeUnexpected("Page domain already enabled"_s);

    m_instrumentingAgents.setEnabledPageAgent(this);

    auto& stopwatch = m_environment.executionStopwatch();
    stopwatch.reset();
    stopwatch.start();

    defaultUserPreferencesDidChange();

    if (!createdUserWorlds().isEmpty()) {
        Vector<DOMWrapperWorld*> worlds;
        for (const auto& world : createdUserWorlds().values())
            worlds.append(world.ptr());
        ensureUserWorldsExistInAllFrames(worlds);
    }
    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::disable()
{
    m_instrumentingAgents.setEnabledPageAgent(nullptr);
    m_interceptFileChooserDialog = false;
    m_bypassCSP = false;

    setShowPaintRects(false);
#if !PLATFORM(IOS_FAMILY)
    setShowRulers(false);
#endif
    overrideUserAgent(nullString());
    setEmulatedMedia(emptyString());
    overridePrefersColorScheme(std::nullopt);

    auto& inspectedPageSettings = m_inspectedPage.settings();
    inspectedPageSettings.setAuthorAndUserStylesEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setICECandidateFilteringEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setImagesEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setMediaCaptureRequiresSecureConnectionInspectorOverride(std::nullopt);
    inspectedPageSettings.setMockCaptureDevicesEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setNeedsSiteSpecificQuirksInspectorOverride(std::nullopt);
    inspectedPageSettings.setScriptEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setShowDebugBordersInspectorOverride(std::nullopt);
    inspectedPageSettings.setShowRepaintCounterInspectorOverride(std::nullopt);
    inspectedPageSettings.setWebSecurityEnabledInspectorOverride(std::nullopt);
    inspectedPageSettings.setForcedPrefersReducedMotionAccessibilityValue(ForcedAccessibilityValue::System);
    inspectedPageSettings.setForcedPrefersContrastAccessibilityValue(ForcedAccessibilityValue::System);

    m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::PrivateClickMeasurementDebugModeEnabled, std::nullopt);
    m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::ITPDebugModeEnabled, std::nullopt);
    m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::MockCaptureDevicesEnabled, std::nullopt);

    return { };
}

double InspectorPageAgent::timestamp()
{
    return m_environment.executionStopwatch().elapsedTime().seconds();
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::reload(std::optional<bool>&& ignoreCache, std::optional<bool>&& revalidateAllResources)
{
    OptionSet<ReloadOption> reloadOptions;
    if (ignoreCache && *ignoreCache)
        reloadOptions.add(ReloadOption::FromOrigin);
    if (!revalidateAllResources || !*revalidateAllResources)
        reloadOptions.add(ReloadOption::ExpiredOnly);

    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("main frame is not local"_s);
    localMainFrame->loader().reload(reloadOptions);

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::goBack()
{
    if (!m_inspectedPage.backForward().goBack())
        return makeUnexpected("Failed to go back"_s);

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::goForward()
{
    if (!m_inspectedPage.backForward().goForward())
        return makeUnexpected("Failed to go forward"_s);

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::navigate(const String& url)
{
    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return { };

    UserGestureIndicator indicator { IsProcessingUserGesture::Yes, localMainFrame->document() };

    ResourceRequest resourceRequest { localMainFrame->document()->completeURL(url) };
    FrameLoadRequest frameLoadRequest { *localMainFrame->document(), localMainFrame->document()->securityOrigin(), WTFMove(resourceRequest), selfTargetFrameName(), InitiatedByMainFrame::Unknown };
    frameLoadRequest.disableNavigationToInvalidURL();
    localMainFrame->loader().changeLocation(WTFMove(frameLoadRequest));

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::overrideUserAgent(const String& value)
{
    m_userAgentOverride = value;

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::overridePlatform(const String& value)
{
    m_platformOverride = value;

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::overrideSetting(Inspector::Protocol::Page::Setting setting, std::optional<bool>&& value)
{
    auto& inspectedPageSettings = m_inspectedPage.settings();

    switch (setting) {
    case Inspector::Protocol::Page::Setting::PrivateClickMeasurementDebugModeEnabled:
        m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::PrivateClickMeasurementDebugModeEnabled, value);
        return { };

    case Inspector::Protocol::Page::Setting::AuthorAndUserStylesEnabled:
        inspectedPageSettings.setAuthorAndUserStylesEnabledInspectorOverride(value);
        return { };

#if ENABLE(DEVICE_ORIENTATION)
    case Protocol::Page::Setting::DeviceOrientationEventEnabled:
        inspectedPageSettings.setDeviceOrientationEventEnabled(value.value_or(false));
        return { };
#endif

    case Inspector::Protocol::Page::Setting::ICECandidateFilteringEnabled:
        inspectedPageSettings.setICECandidateFilteringEnabledInspectorOverride(value);
        return { };

    case Inspector::Protocol::Page::Setting::ITPDebugModeEnabled:
        m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::ITPDebugModeEnabled, value);
        return { };

    case Inspector::Protocol::Page::Setting::ImagesEnabled:
        inspectedPageSettings.setImagesEnabledInspectorOverride(value);
        return { };

    case Inspector::Protocol::Page::Setting::MediaCaptureRequiresSecureConnection:
        inspectedPageSettings.setMediaCaptureRequiresSecureConnectionInspectorOverride(value);
        return { };

    case Inspector::Protocol::Page::Setting::MockCaptureDevicesEnabled:
        inspectedPageSettings.setMockCaptureDevicesEnabledInspectorOverride(value);
        m_client->setDeveloperPreferenceOverride(InspectorClient::DeveloperPreference::MockCaptureDevicesEnabled, value);
        return { };

    case Inspector::Protocol::Page::Setting::NeedsSiteSpecificQuirks:
        inspectedPageSettings.setNeedsSiteSpecificQuirksInspectorOverride(value);
        return { };

#if ENABLE(NOTIFICATIONS)
    case Protocol::Page::Setting::NotificationsEnabled:
        inspectedPageSettings.setNotificationsEnabled(value.value_or(false));
        return { };
#endif

#if ENABLE(FULLSCREEN_API)
    case Protocol::Page::Setting::FullScreenEnabled:
        inspectedPageSettings.setFullScreenEnabled(value.value_or(false));
        return { };
#endif

    case Protocol::Page::Setting::InputTypeMonthEnabled:
// Playwright client sends it even if it's not supported.
#if ENABLE(INPUT_TYPE_MONTH)
        inspectedPageSettings.setInputTypeMonthEnabled(value.value_or(false));
#endif
        return { };

    case Protocol::Page::Setting::InputTypeWeekEnabled:
// Playwright client sends it even if it's not supported.
#if ENABLE(INPUT_TYPE_WEEK)
        inspectedPageSettings.setInputTypeWeekEnabled(value.value_or(false));
#endif
        return { };

#if ENABLE(POINTER_LOCK)
    case Protocol::Page::Setting::PointerLockEnabled:
        inspectedPageSettings.setPointerLockEnabled(value.value_or(false));
        return { };
#endif

    case Inspector::Protocol::Page::Setting::ScriptEnabled:
        inspectedPageSettings.setScriptEnabledInspectorOverride(value);
        return { };

    case Inspector::Protocol::Page::Setting::ShowDebugBorders:
        inspectedPageSettings.setShowDebugBordersInspectorOverride(value);
        return { };

    case Inspector::Protocol::Page::Setting::ShowRepaintCounter:
        inspectedPageSettings.setShowRepaintCounterInspectorOverride(value);
        return { };

#if ENABLE(MEDIA_STREAM)
    case Protocol::Page::Setting::SpeechRecognitionEnabled:
        inspectedPageSettings.setSpeechRecognitionEnabled(value.value_or(false));
        return { };
#endif

    case Inspector::Protocol::Page::Setting::WebSecurityEnabled:
        inspectedPageSettings.setWebSecurityEnabledInspectorOverride(value);
        return { };
    }

    ASSERT_NOT_REACHED();
    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::overrideUserPreference(Inspector::Protocol::Page::UserPreferenceName preference, std::optional<Inspector::Protocol::Page::UserPreferenceValue>&& value)
{
    switch (preference) {
    case Inspector::Protocol::Page::UserPreferenceName::PrefersReducedMotion:
        overridePrefersReducedMotion(WTFMove(value));
        return { };

    case Inspector::Protocol::Page::UserPreferenceName::PrefersContrast:
        overridePrefersContrast(WTFMove(value));
        return { };

    case Inspector::Protocol::Page::UserPreferenceName::PrefersColorScheme:
        overridePrefersColorScheme(WTFMove(value));
        return { };
    }

    ASSERT_NOT_REACHED();
    return { };
}

void InspectorPageAgent::overridePrefersReducedMotion(std::optional<Inspector::Protocol::Page::UserPreferenceValue>&& value)
{
    ForcedAccessibilityValue forcedValue = ForcedAccessibilityValue::System;

    if (value == Inspector::Protocol::Page::UserPreferenceValue::Reduce)
        forcedValue = ForcedAccessibilityValue::On;
    else if (value == Inspector::Protocol::Page::UserPreferenceValue::NoPreference)
        forcedValue = ForcedAccessibilityValue::Off;

    m_inspectedPage.settings().setForcedPrefersReducedMotionAccessibilityValue(forcedValue);
    m_inspectedPage.accessibilitySettingsDidChange();
}

void InspectorPageAgent::overridePrefersContrast(std::optional<Inspector::Protocol::Page::UserPreferenceValue>&& value)
{
    ForcedAccessibilityValue forcedValue = ForcedAccessibilityValue::System;

    if (value == Inspector::Protocol::Page::UserPreferenceValue::More)
        forcedValue = ForcedAccessibilityValue::On;
    else if (value == Inspector::Protocol::Page::UserPreferenceValue::NoPreference)
        forcedValue = ForcedAccessibilityValue::Off;

    m_inspectedPage.settings().setForcedPrefersContrastAccessibilityValue(forcedValue);
    m_inspectedPage.accessibilitySettingsDidChange();
}

void InspectorPageAgent::overridePrefersColorScheme(std::optional<Inspector::Protocol::Page::UserPreferenceValue>&& value)
{
#if ENABLE(DARK_MODE_CSS) || HAVE(OS_DARK_MODE_SUPPORT)
    if (!value)
        m_inspectedPage.setUseDarkAppearanceOverride(std::nullopt);
    else if (value == Inspector::Protocol::Page::UserPreferenceValue::Light)
        m_inspectedPage.setUseDarkAppearanceOverride(false);
    else if (value == Inspector::Protocol::Page::UserPreferenceValue::Dark)
        m_inspectedPage.setUseDarkAppearanceOverride(true);
#else
    UNUSED_PARAM(value);
#endif
}

static Inspector::Protocol::Page::CookieSameSitePolicy cookieSameSitePolicyJSON(Cookie::SameSitePolicy policy)
{
    switch (policy) {
    case Cookie::SameSitePolicy::None:
        return Inspector::Protocol::Page::CookieSameSitePolicy::None;
    case Cookie::SameSitePolicy::Lax:
        return Inspector::Protocol::Page::CookieSameSitePolicy::Lax;
    case Cookie::SameSitePolicy::Strict:
        return Inspector::Protocol::Page::CookieSameSitePolicy::Strict;
    }
    ASSERT_NOT_REACHED();
    return Inspector::Protocol::Page::CookieSameSitePolicy::None;
}

static Ref<Inspector::Protocol::Page::Cookie> buildObjectForCookie(const Cookie& cookie)
{
    return Inspector::Protocol::Page::Cookie::create()
        .setName(cookie.name)
        .setValue(cookie.value)
        .setDomain(cookie.domain)
        .setPath(cookie.path)
        .setExpires(cookie.expires.value_or(0))
        .setSession(cookie.session)
        .setHttpOnly(cookie.httpOnly)
        .setSecure(cookie.secure)
        .setSameSite(cookieSameSitePolicyJSON(cookie.sameSite))
        .release();
}

static Ref<JSON::ArrayOf<Inspector::Protocol::Page::Cookie>> buildArrayForCookies(ListHashSet<Cookie>& cookiesList)
{
    auto cookies = JSON::ArrayOf<Inspector::Protocol::Page::Cookie>::create();

    for (const auto& cookie : cookiesList)
        cookies->addItem(buildObjectForCookie(cookie));

    return cookies;
}

static Vector<URL> allResourcesURLsForFrame(LocalFrame* frame)
{
    Vector<URL> result;

    result.append(frame->loader().documentLoader()->url());

    for (auto* cachedResource : InspectorPageAgent::cachedResourcesForFrame(frame))
        result.append(cachedResource->url());

    return result;
}

Inspector::Protocol::ErrorStringOr<Ref<JSON::ArrayOf<Inspector::Protocol::Page::Cookie>>> InspectorPageAgent::getCookies()
{
    ListHashSet<Cookie> allRawCookies;

    for (Frame* frame = &m_inspectedPage.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        auto* document = localFrame->document();
        if (!document || !document->page())
            continue;

        for (auto& url : allResourcesURLsForFrame(localFrame)) {
            Vector<Cookie> rawCookiesForURLInDocument;
            if (!document->page()->cookieJar().getRawCookies(*document, url, rawCookiesForURLInDocument))
                continue;

            for (auto& rawCookieForURLInDocument : rawCookiesForURLInDocument)
                allRawCookies.add(rawCookieForURLInDocument);
        }
    }

    return buildArrayForCookies(allRawCookies);
}

static std::optional<Cookie> parseCookieObject(Inspector::Protocol::ErrorString& errorString, Ref<JSON::Object>&& cookieObject)
{
    Cookie cookie;

    cookie.name = cookieObject->getString(Inspector::Protocol::Page::Cookie::nameKey);
    if (!cookie.name) {
        errorString = "Invalid value for key name in given cookie"_s;
        return std::nullopt;
    }

    cookie.value = cookieObject->getString(Inspector::Protocol::Page::Cookie::valueKey);
    if (!cookie.value) {
        errorString = "Invalid value for key value in given cookie"_s;
        return std::nullopt;
    }

    cookie.domain = cookieObject->getString(Inspector::Protocol::Page::Cookie::domainKey);
    if (!cookie.domain) {
        errorString = "Invalid value for key domain in given cookie"_s;
        return std::nullopt;
    }

    cookie.path = cookieObject->getString(Inspector::Protocol::Page::Cookie::pathKey);
    if (!cookie.path) {
        errorString = "Invalid value for key path in given cookie"_s;
        return std::nullopt;
    }

    auto httpOnly = cookieObject->getBoolean(Inspector::Protocol::Page::Cookie::httpOnlyKey);
    if (!httpOnly) {
        errorString = "Invalid value for key httpOnly in given cookie"_s;
        return std::nullopt;
    }

    cookie.httpOnly = *httpOnly;

    auto secure = cookieObject->getBoolean(Inspector::Protocol::Page::Cookie::secureKey);
    if (!secure) {
        errorString = "Invalid value for key secure in given cookie"_s;
        return std::nullopt;
    }

    cookie.secure = *secure;

    auto session = cookieObject->getBoolean(Inspector::Protocol::Page::Cookie::sessionKey);
    cookie.expires = cookieObject->getDouble(Inspector::Protocol::Page::Cookie::expiresKey);
    if (!session && !cookie.expires) {
        errorString = "Invalid value for key expires in given cookie"_s;
        return std::nullopt;
    }

    cookie.session = *session;

    auto sameSiteString = cookieObject->getString(Inspector::Protocol::Page::Cookie::sameSiteKey);
    if (!sameSiteString) {
        errorString = "Invalid value for key sameSite in given cookie"_s;
        return std::nullopt;
    }

    auto sameSite = Inspector::Protocol::Helpers::parseEnumValueFromString<Inspector::Protocol::Page::CookieSameSitePolicy>(sameSiteString);
    if (!sameSite) {
        errorString = "Invalid value for key sameSite in given cookie"_s;
        return std::nullopt;
    }

    switch (sameSite.value()) {
    case Inspector::Protocol::Page::CookieSameSitePolicy::None:
        cookie.sameSite = Cookie::SameSitePolicy::None;
        break;

    case Inspector::Protocol::Page::CookieSameSitePolicy::Lax:
        cookie.sameSite = Cookie::SameSitePolicy::Lax;
        break;

    case Inspector::Protocol::Page::CookieSameSitePolicy::Strict:
        cookie.sameSite = Cookie::SameSitePolicy::Strict;
        break;
    }

    return cookie;
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setCookie(Ref<JSON::Object>&& cookieObject)
{
    Inspector::Protocol::ErrorString errorString;

    auto cookie = parseCookieObject(errorString, WTFMove(cookieObject));
    if (!cookie)
        return makeUnexpected(errorString);

    for (Frame* frame = &m_inspectedPage.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        auto* document = localFrame->document();
        if (!document)
            continue;
        auto* page = document->page();
        if (!page)
            continue;
        page->cookieJar().setRawCookie(*document, cookie.value());
    }

    return { };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::deleteCookie(const String& cookieName, const String& url)
{
    URL parsedURL({ }, url);
    for (Frame* frame = &m_inspectedPage.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        auto* document = localFrame->document();
        if (!document)
            continue;
        auto* page = document->page();
        if (!page)
            continue;
        page->cookieJar().deleteCookie(*document, parsedURL, cookieName, [] { });
    }

    return { };
}

Inspector::Protocol::ErrorStringOr<Ref<Inspector::Protocol::Page::FrameResourceTree>> InspectorPageAgent::getResourceTree()
{
    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    return buildObjectForFrameTree(localMainFrame);
}

Inspector::Protocol::ErrorStringOr<std::tuple<String, bool /* base64Encoded */>> InspectorPageAgent::getResourceContent(const Inspector::Protocol::Network::FrameId& frameId, const String& url)
{
    Inspector::Protocol::ErrorString errorString;

    auto* frame = assertFrame(errorString, frameId);
    if (!frame)
        return makeUnexpected(errorString);

    String content;
    bool base64Encoded;

    resourceContent(errorString, frame, URL({ }, url), &content, &base64Encoded);

    return { { content, base64Encoded } };
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setBootstrapScript(const String& source)
{
    m_bootstrapScript = source;

    return { };
}

Inspector::Protocol::ErrorStringOr<Ref<JSON::ArrayOf<Inspector::Protocol::GenericTypes::SearchMatch>>> InspectorPageAgent::searchInResource(const Inspector::Protocol::Network::FrameId& frameId, const String& url, const String& query, std::optional<bool>&& caseSensitive, std::optional<bool>&& isRegex, const Inspector::Protocol::Network::RequestId& requestId)
{
    Inspector::Protocol::ErrorString errorString;

    if (!!requestId) {
        if (auto* networkAgent = m_instrumentingAgents.enabledNetworkAgent()) {
            RefPtr<JSON::ArrayOf<Inspector::Protocol::GenericTypes::SearchMatch>> result;
            networkAgent->searchInRequest(errorString, requestId, query, caseSensitive && *caseSensitive, isRegex && *isRegex, result);
            if (!result)
                return makeUnexpected(errorString);
            return result.releaseNonNull();
        }
    }

    auto* frame = assertFrame(errorString, frameId);
    if (!frame)
        return makeUnexpected(errorString);

    DocumentLoader* loader = assertDocumentLoader(errorString, frame);
    if (!loader)
        return makeUnexpected(errorString);

    URL kurl({ }, url);

    String content;
    bool success = false;
    if (equalIgnoringFragmentIdentifier(kurl, loader->url()))
        success = mainResourceContent(frame, false, &content);

    if (!success) {
        if (auto* resource = cachedResource(frame, kurl)) {
            if (auto textContent = InspectorNetworkAgent::textContentForCachedResource(*resource)) {
                content = *textContent;
                success = true;
            }
        }
    }

    if (!success)
        return JSON::ArrayOf<Inspector::Protocol::GenericTypes::SearchMatch>::create();

    return ContentSearchUtilities::searchInTextByLines(content, query, caseSensitive && *caseSensitive, isRegex && *isRegex);
}

static Ref<Inspector::Protocol::Page::SearchResult> buildObjectForSearchResult(const Inspector::Protocol::Network::FrameId& frameId, const String& url, int matchesCount)
{
    return Inspector::Protocol::Page::SearchResult::create()
        .setUrl(url)
        .setFrameId(frameId)
        .setMatchesCount(matchesCount)
        .release();
}

Inspector::Protocol::ErrorStringOr<Ref<JSON::ArrayOf<Inspector::Protocol::Page::SearchResult>>> InspectorPageAgent::searchInResources(const String& text, std::optional<bool>&& caseSensitive, std::optional<bool>&& isRegex)
{
    auto result = JSON::ArrayOf<Inspector::Protocol::Page::SearchResult>::create();

    auto searchStringType = (isRegex && *isRegex) ? ContentSearchUtilities::SearchStringType::Regex : ContentSearchUtilities::SearchStringType::ContainsString;
    auto regex = ContentSearchUtilities::createRegularExpressionForSearchString(text, caseSensitive && *caseSensitive, searchStringType);

    for (Frame* frame = &m_inspectedPage.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        for (auto* cachedResource : cachedResourcesForFrame(localFrame)) {
            if (auto textContent = InspectorNetworkAgent::textContentForCachedResource(*cachedResource)) {
                int matchesCount = ContentSearchUtilities::countRegularExpressionMatches(regex, *textContent);
                if (matchesCount)
                    result->addItem(buildObjectForSearchResult(frameId(localFrame), cachedResource->url().string(), matchesCount));
            }
        }
    }

    if (auto* networkAgent = m_instrumentingAgents.enabledNetworkAgent())
        networkAgent->searchOtherRequests(regex, result);

    return result;
}

#if !PLATFORM(IOS_FAMILY)
Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setShowRulers(bool showRulers)
{
    m_overlay->setShowRulers(showRulers);

    return { };
}
#endif

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setShowPaintRects(bool show)
{
    m_showPaintRects = show;
    m_client->setShowPaintRects(show);

    if (m_client->overridesShowPaintRects())
        return { };

    m_overlay->setShowPaintRects(show);

    return { };
}

void InspectorPageAgent::domContentEventFired(LocalFrame& frame)
{
    if (frame.isMainFrame())
        m_isFirstLayoutAfterOnLoad = true;
    m_frontendDispatcher->domContentEventFired(timestamp(), frameId(&frame));
}

void InspectorPageAgent::loadEventFired(LocalFrame& frame)
{
    m_frontendDispatcher->loadEventFired(timestamp(), frameId(&frame));
}

void InspectorPageAgent::frameNavigated(LocalFrame& frame)
{
    m_frontendDispatcher->frameNavigated(buildObjectForFrame(&frame));
}

String InspectorPageAgent::makeFrameID(ProcessIdentifier processID,  FrameIdentifier frameID)
{
    return makeString(processID.toUInt64(), ".", frameID.object().toUInt64());
}

static String globalIDForFrame(Frame& frame)
{
    // TODO(playwright): for OOPIF we have to use id of the web process where the frame is hosted.
    // Working at the moment because OOPIF is diabled.
    return InspectorPageAgent::makeFrameID(Process::identifier(), frame.frameID());
}

void InspectorPageAgent::frameDetached(LocalFrame& frame)
{
    String identifier = globalIDForFrame(frame);
    if (!m_identifierToFrame.take(identifier))
        return;

    m_frontendDispatcher->frameDetached(identifier);
}

Frame* InspectorPageAgent::frameForId(const Inspector::Protocol::Network::FrameId& frameId)
{
    return frameId.isEmpty() ? nullptr : m_identifierToFrame.get(frameId).get();
}

String InspectorPageAgent::frameId(Frame* frame)
{
    if (!frame)
        return emptyString();
    String identifier = globalIDForFrame(*frame);
    m_identifierToFrame.set(identifier, frame);
    return identifier;
}

String InspectorPageAgent::loaderId(DocumentLoader* loader)
{
    if (!loader)
        return emptyString();

    return String::number(loader->navigationID());
}

LocalFrame* InspectorPageAgent::assertFrame(Inspector::Protocol::ErrorString& errorString, const Inspector::Protocol::Network::FrameId& frameId)
{
    auto* frame = dynamicDowncast<LocalFrame>(frameForId(frameId));
    if (!frame)
        errorString = "Missing frame for given frameId"_s;
    return frame;
}

void InspectorPageAgent::frameStartedLoading(LocalFrame& frame)
{
    m_frontendDispatcher->frameStartedLoading(frameId(&frame));
}

void InspectorPageAgent::frameStoppedLoading(LocalFrame& frame)
{
    m_frontendDispatcher->frameStoppedLoading(frameId(&frame));
}

void InspectorPageAgent::frameScheduledNavigation(Frame& frame, Seconds delay, bool targetIsCurrentFrame)
{
    m_frontendDispatcher->frameScheduledNavigation(frameId(&frame), delay.value(), targetIsCurrentFrame);
}

void InspectorPageAgent::frameClearedScheduledNavigation(Frame& frame)
{
    m_frontendDispatcher->frameClearedScheduledNavigation(frameId(&frame));
}

void InspectorPageAgent::accessibilitySettingsDidChange()
{
    defaultUserPreferencesDidChange();
}

void InspectorPageAgent::defaultUserPreferencesDidChange()
{
    auto defaultUserPreferences = JSON::ArrayOf<Inspector::Protocol::Page::UserPreference>::create();

    bool prefersReducedMotion = Theme::singleton().userPrefersReducedMotion();

    auto prefersReducedMotionUserPreference = Inspector::Protocol::Page::UserPreference::create()
        .setName(Inspector::Protocol::Page::UserPreferenceName::PrefersReducedMotion)
        .setValue(prefersReducedMotion ? Inspector::Protocol::Page::UserPreferenceValue::Reduce : Inspector::Protocol::Page::UserPreferenceValue::NoPreference)
        .release();

    defaultUserPreferences->addItem(WTFMove(prefersReducedMotionUserPreference));

    bool prefersContrast = Theme::singleton().userPrefersContrast();

    auto prefersContrastUserPreference = Inspector::Protocol::Page::UserPreference::create()
        .setName(Inspector::Protocol::Page::UserPreferenceName::PrefersContrast)
        .setValue(prefersContrast ? Inspector::Protocol::Page::UserPreferenceValue::More : Inspector::Protocol::Page::UserPreferenceValue::NoPreference)
        .release();

    defaultUserPreferences->addItem(WTFMove(prefersContrastUserPreference));

#if ENABLE(DARK_MODE_CSS) || HAVE(OS_DARK_MODE_SUPPORT)
    auto prefersColorSchemeUserPreference = Inspector::Protocol::Page::UserPreference::create()
        .setName(Inspector::Protocol::Page::UserPreferenceName::PrefersColorScheme)
        .setValue(m_inspectedPage.defaultUseDarkAppearance() ? Inspector::Protocol::Page::UserPreferenceValue::Dark : Inspector::Protocol::Page::UserPreferenceValue::Light)
        .release();

    defaultUserPreferences->addItem(WTFMove(prefersColorSchemeUserPreference));
#endif

    m_frontendDispatcher->defaultUserPreferencesDidChange(WTFMove(defaultUserPreferences));
}

void InspectorPageAgent::didNavigateWithinPage(LocalFrame& frame)
{
    String url = frame.document()->url().string();
    m_frontendDispatcher->navigatedWithinDocument(frameId(&frame), url);
}

#if ENABLE(DARK_MODE_CSS) || HAVE(OS_DARK_MODE_SUPPORT)
void InspectorPageAgent::defaultAppearanceDidChange()
{
    defaultUserPreferencesDidChange();
}
#endif

void InspectorPageAgent::didClearWindowObjectInWorld(LocalFrame& frame, DOMWrapperWorld& world)
{
    if (&world != &mainThreadNormalWorld())
        return;

    if (m_bootstrapScript.isEmpty())
       return;

    if (m_ignoreDidClearWindowObject)
        return;

    frame.script().evaluateIgnoringException(ScriptSourceCode(m_bootstrapScript, JSC::SourceTaintedOrigin::Untainted, URL { "web-inspector://bootstrap.js"_str }));
}

void InspectorPageAgent::didPaint(RenderObject& renderer, const LayoutRect& rect)
{
    if (!m_showPaintRects)
        return;

    LayoutRect absoluteRect = LayoutRect(renderer.localToAbsoluteQuad(FloatRect(rect)).boundingBox());
    auto* view = renderer.document().view();

    LayoutRect rootRect = absoluteRect;
    Ref localFrame = view->frame();
    if (!localFrame->isMainFrame()) {
        IntRect rootViewRect = view->contentsToRootView(snappedIntRect(absoluteRect));
        rootRect = localFrame->mainFrame().virtualView()->rootViewToContents(rootViewRect);
    }

    if (m_client->overridesShowPaintRects()) {
        m_client->showPaintRect(rootRect);
        return;
    }

    m_overlay->showPaintRect(rootRect);
}

void InspectorPageAgent::didLayout()
{
    bool isFirstLayout = m_isFirstLayoutAfterOnLoad;
    if (isFirstLayout)
        m_isFirstLayoutAfterOnLoad = false;

    m_overlay->update();
}

void InspectorPageAgent::didScroll()
{
    m_overlay->update();
}

void InspectorPageAgent::didRecalculateStyle()
{
    m_overlay->update();
}

void InspectorPageAgent::runOpenPanel(HTMLInputElement* element, bool* intercept)
{
    if (m_interceptFileChooserDialog) {
        *intercept = true;
    } else {
        return;
    }
    Document& document = element->document();
    auto* frame =  document.frame();
    if (!frame)
        return;

    auto& globalObject = mainWorldGlobalObject(*frame);
    auto injectedScript = m_injectedScriptManager.injectedScriptFor(&globalObject);
    if (injectedScript.hasNoValue())
        return;

    auto object = injectedScript.wrapObject(InspectorDOMAgent::nodeAsScriptValue(globalObject, element), WTF::String());
    if (!object)
        return;

    m_frontendDispatcher->fileChooserOpened(frameId(frame), object.releaseNonNull());
}

void InspectorPageAgent::frameAttached(LocalFrame& frame)
{
    String parentFrameId = frameId(dynamicDowncast<LocalFrame>(frame.tree().parent()));
    m_frontendDispatcher->frameAttached(frameId(&frame), parentFrameId);
}

bool InspectorPageAgent::shouldBypassCSP()
{
    return m_bypassCSP;
}

void InspectorPageAgent::willCheckNavigationPolicy(LocalFrame& frame)
{
    m_frontendDispatcher->willCheckNavigationPolicy(frameId(&frame));
}

void InspectorPageAgent::didCheckNavigationPolicy(LocalFrame& frame, bool cancel)
{
    m_frontendDispatcher->didCheckNavigationPolicy(frameId(&frame), cancel);
}

Ref<Inspector::Protocol::Page::Frame> InspectorPageAgent::buildObjectForFrame(LocalFrame* frame)
{
    ASSERT_ARG(frame, frame);

    auto frameObject = Inspector::Protocol::Page::Frame::create()
        .setId(frameId(frame))
        .setLoaderId(loaderId(frame->loader().documentLoader()))
        .setUrl(frame->document()->url().string())
        .setMimeType(frame->loader().documentLoader()->responseMIMEType())
        .setSecurityOrigin(frame->document()->securityOrigin().toRawString())
        .release();
    if (frame->tree().parent())
        frameObject->setParentId(frameId(dynamicDowncast<LocalFrame>(frame->tree().parent())));
    if (frame->ownerElement()) {
        String name = frame->ownerElement()->getNameAttribute();
        if (name.isEmpty())
            name = frame->ownerElement()->attributeWithoutSynchronization(HTMLNames::idAttr);
        frameObject->setName(name);
    }

    return frameObject;
}

Ref<Inspector::Protocol::Page::FrameResourceTree> InspectorPageAgent::buildObjectForFrameTree(LocalFrame* frame)
{
    ASSERT_ARG(frame, frame);

    auto frameObject = buildObjectForFrame(frame);
    auto subresources = JSON::ArrayOf<Inspector::Protocol::Page::FrameResource>::create();
    auto result = Inspector::Protocol::Page::FrameResourceTree::create()
        .setFrame(WTFMove(frameObject))
        .setResources(subresources.copyRef())
        .release();

    for (auto* cachedResource : cachedResourcesForFrame(frame)) {
        auto resourceObject = Inspector::Protocol::Page::FrameResource::create()
            .setUrl(cachedResource->url().string())
            .setType(cachedResourceTypeJSON(*cachedResource))
            .setMimeType(cachedResource->response().mimeType())
            .release();
        if (cachedResource->wasCanceled())
            resourceObject->setCanceled(true);
        else if (cachedResource->status() == CachedResource::LoadError || cachedResource->status() == CachedResource::DecodeError)
            resourceObject->setFailed(true);
        String sourceMappingURL = InspectorPageAgent::sourceMapURLForResource(cachedResource);
        if (!sourceMappingURL.isEmpty())
            resourceObject->setSourceMapURL(sourceMappingURL);
        String targetId = cachedResource->resourceRequest().initiatorIdentifier();
        if (!targetId.isEmpty())
            resourceObject->setTargetId(targetId);
        subresources->addItem(WTFMove(resourceObject));
    }

    RefPtr<JSON::ArrayOf<Inspector::Protocol::Page::FrameResourceTree>> childrenArray;
    for (Frame* child = frame->tree().firstChild(); child; child = child->tree().nextSibling()) {
        if (!childrenArray) {
            childrenArray = JSON::ArrayOf<Inspector::Protocol::Page::FrameResourceTree>::create();
            result->setChildFrames(*childrenArray);
        }
        auto* localChild = dynamicDowncast<LocalFrame>(child);
        if (!localChild)
            continue;
        childrenArray->addItem(buildObjectForFrameTree(localChild));
    }
    return result;
}

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setEmulatedMedia(const String& media)
{
    if (media == m_emulatedMedia)
        return { };

    m_emulatedMedia = AtomString(media);

    // FIXME: Schedule a rendering update instead of synchronously updating the layout.
    m_inspectedPage.updateStyleAfterChangeInEnvironment();

    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return { };

    RefPtr document = localMainFrame->document();
    if (!document)
        return { };

    document->updateLayout();
    document->evaluateMediaQueriesAndReportChanges();

    return { };
}

void InspectorPageAgent::applyUserAgentOverride(String& userAgent)
{
    if (!m_userAgentOverride.isEmpty())
        userAgent = m_userAgentOverride;
}

void InspectorPageAgent::applyPlatformOverride(String& platform)
{
    if (!m_platformOverride.isEmpty())
        platform = m_platformOverride;
}

void InspectorPageAgent::applyEmulatedMedia(AtomString& media)
{
    if (!m_emulatedMedia.isEmpty())
        media = m_emulatedMedia;
}

Inspector::Protocol::ErrorStringOr<String> InspectorPageAgent::snapshotNode(Inspector::Protocol::DOM::NodeId nodeId)
{
    Inspector::Protocol::ErrorString errorString;

    InspectorDOMAgent* domAgent = m_instrumentingAgents.persistentDOMAgent();
    ASSERT(domAgent);
    Node* node = domAgent->assertNode(errorString, nodeId);
    if (!node)
        return makeUnexpected(errorString);
    
    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("Main frame isn't local"_s);

    auto snapshot = WebCore::snapshotNode(*localMainFrame, *node, { { }, PixelFormat::BGRA8, DestinationColorSpace::SRGB() });
    if (!snapshot)
        return makeUnexpected("Could not capture snapshot"_s);

    return snapshot->toDataURL("image/png"_s, std::nullopt, PreserveResolution::Yes);
}

Inspector::Protocol::ErrorStringOr<String> InspectorPageAgent::snapshotRect(int x, int y, int width, int height, Inspector::Protocol::Page::CoordinateSystem coordinateSystem, std::optional<bool>&& omitDeviceScaleFactor)
{
    SnapshotOptions options { { }, PixelFormat::BGRA8, DestinationColorSpace::SRGB() };
    if (coordinateSystem == Inspector::Protocol::Page::CoordinateSystem::Viewport)
        options.flags.add(SnapshotFlags::InViewCoordinates);
    if (omitDeviceScaleFactor.has_value() && *omitDeviceScaleFactor)
        options.flags.add(SnapshotFlags::OmitDeviceScaleFactor);

    IntRect rectangle(x, y, width, height);
    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("Main frame isn't local"_s);
    auto snapshot = snapshotFrameRect(*localMainFrame, rectangle, WTFMove(options));

    if (!snapshot)
        return makeUnexpected("Could not capture snapshot"_s);

    return snapshot->toDataURL("image/png"_s, std::nullopt, PreserveResolution::Yes);
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setForcedColors(std::optional<Protocol::Page::ForcedColors>&& forcedColors)
{
    if (!forcedColors) {
        m_inspectedPage.setUseForcedColorsOverride(std::nullopt);
        return { };
    }

    switch (*forcedColors) {
        case Protocol::Page::ForcedColors::Active:
            m_inspectedPage.setUseForcedColorsOverride(true);
            return { };
        case Protocol::Page::ForcedColors::None:
            m_inspectedPage.setUseForcedColorsOverride(false);
            return { };
    }

    ASSERT_NOT_REACHED();
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setTimeZone(const String& timeZone)
{
    bool success = WTF::setTimeZoneOverride(timeZone);
    if (!success)
        return makeUnexpected("Invalid time zone " + timeZone);

    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setTouchEmulationEnabled(bool enabled)
{
  setScreenHasTouchDeviceOverride(enabled);
  m_inspectedPage.settings().setTouchEventsEnabled(enabled);
  return { };
}


#if ENABLE(WEB_ARCHIVE) && USE(CF)
Inspector::Protocol::ErrorStringOr<String> InspectorPageAgent::archive()
{
    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("Main frame isn't local"_s);

    auto archive = LegacyWebArchive::create(*localMainFrame);
    if (!archive)
        return makeUnexpected("Could not create web archive for main frame"_s);

    RetainPtr<CFDataRef> buffer = archive->rawDataRepresentation();
    return base64EncodeToString(CFDataGetBytePtr(buffer.get()), CFDataGetLength(buffer.get()));
}
#endif

Inspector::Protocol::ErrorStringOr<void> InspectorPageAgent::setScreenSizeOverride(std::optional<int>&& width, std::optional<int>&& height)
{
    if (width.has_value() != height.has_value())
        return makeUnexpected("Screen width and height override should be both specified or omitted"_s);

    if (width && *width <= 0)
        return makeUnexpected("Screen width override should be a positive integer"_s);

    if (height && *height <= 0)
        return makeUnexpected("Screen height override should be a positive integer"_s);

    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("Main frame isn't local"_s);
    localMainFrame->setOverrideScreenSize(FloatSize(width.value_or(0), height.value_or(0)));
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::insertText(const String& text)
{
    UserGestureIndicator indicator { IsProcessingUserGesture::Yes };
    RefPtr frame = m_inspectedPage.checkedFocusController()->focusedOrMainFrame();
    if (!frame)
        return { };

    if (frame->editor().hasComposition()) {
        frame->editor().confirmComposition(text);
    } else {
        Document* focusedDocument = frame->document();
        TypingCommand::insertText(*focusedDocument, text, { });
    }
    return { };
}

static String roleFromObject(RefPtr<AXCoreObject> axObject)
{
    String computedRoleString = axObject->computedRoleString();
    if (!computedRoleString.isEmpty())
        return computedRoleString;
    AccessibilityRole role = axObject->roleValue();
    switch(role) {
        case AccessibilityRole::Application:
            return "Application"_s;
        case AccessibilityRole::ApplicationAlert:
            return "ApplicationAlert"_s;
        case AccessibilityRole::ApplicationAlertDialog:
            return "ApplicationAlertDialog"_s;
        case AccessibilityRole::ApplicationDialog:
            return "ApplicationDialog"_s;
        case AccessibilityRole::ApplicationGroup:
            return "ApplicationGroup"_s;
        case AccessibilityRole::ApplicationLog:
            return "ApplicationLog"_s;
        case AccessibilityRole::ApplicationMarquee:
            return "ApplicationMarquee"_s;
        case AccessibilityRole::ApplicationStatus:
            return "ApplicationStatus"_s;
        case AccessibilityRole::ApplicationTextGroup:
            return "ApplicationTextGroup"_s;
        case AccessibilityRole::ApplicationTimer:
            return "ApplicationTimer"_s;
        case AccessibilityRole::Audio:
            return "Audio"_s;
        case AccessibilityRole::Blockquote:
            return "Blockquote"_s;
        case AccessibilityRole::Button:
            return "Button"_s;
        case AccessibilityRole::Canvas:
            return "Canvas"_s;
        case AccessibilityRole::Caption:
            return "Caption"_s;
        case AccessibilityRole::Cell:
            return "Cell"_s;
        case AccessibilityRole::Checkbox:
            return "CheckBox"_s;
        case AccessibilityRole::Code:
            return "Code"_s;
        case AccessibilityRole::ColorWell:
            return "ColorWell"_s;
        case AccessibilityRole::Column:
            return "Column"_s;
        case AccessibilityRole::ColumnHeader:
            return "ColumnHeader"_s;
        case AccessibilityRole::ComboBox:
            return "ComboBox"_s;
        case AccessibilityRole::DateTime:
            return "DateTime"_s;
        case AccessibilityRole::Definition:
            return "Definition"_s;
        case AccessibilityRole::Deletion:
            return "Deletion"_s;
        case AccessibilityRole::DescriptionList:
            return "DescriptionList"_s;
        case AccessibilityRole::DescriptionListTerm:
            return "DescriptionListTerm"_s;
        case AccessibilityRole::DescriptionListDetail:
            return "DescriptionListDetail"_s;
        case AccessibilityRole::Details:
            return "Details"_s;
        case AccessibilityRole::Directory:
            return "Directory"_s;
        case AccessibilityRole::Document:
            return "Document"_s;
        case AccessibilityRole::DocumentArticle:
            return "DocumentArticle"_s;
        case AccessibilityRole::DocumentMath:
            return "DocumentMath"_s;
        case AccessibilityRole::DocumentNote:
            return "DocumentNote"_s;
        case AccessibilityRole::Emphasis:
            return "Emphasis"_s;
        case AccessibilityRole::Feed:
            return "Feed"_s;
        case AccessibilityRole::Figure:
            return "Figure"_s;
        case AccessibilityRole::Footer:
            return "Footer"_s;
        case AccessibilityRole::Footnote:
            return "Footnote"_s;
        case AccessibilityRole::Form:
            return "Form"_s;
        case AccessibilityRole::Generic:
            return "Generic"_s;
        case AccessibilityRole::GraphicsDocument:
            return "GraphicsDocument"_s;
        case AccessibilityRole::GraphicsObject:
            return "GraphicsObject"_s;
        case AccessibilityRole::GraphicsSymbol:
            return "GraphicsSymbol"_s;
        case AccessibilityRole::Grid:
            return "Grid"_s;
        case AccessibilityRole::GridCell:
            return "GridCell"_s;
        case AccessibilityRole::Group:
            return "Group"_s;
        case AccessibilityRole::Heading:
            return "Heading"_s;
        case AccessibilityRole::HorizontalRule:
            return "HorizontalRule"_s;
        case AccessibilityRole::Ignored:
            return "Ignored"_s;
        case AccessibilityRole::Inline:
            return "Inline"_s;
        case AccessibilityRole::Image:
            return "Image"_s;
        case AccessibilityRole::ImageMap:
            return "ImageMap"_s;
        case AccessibilityRole::ImageMapLink:
            return "ImageMapLink"_s;
        case AccessibilityRole::Incrementor:
            return "Incrementor"_s;
        case AccessibilityRole::Insertion:
            return "Insertion"_s;
        case AccessibilityRole::Label:
            return "Label"_s;
        case AccessibilityRole::LandmarkBanner:
            return "LandmarkBanner"_s;
        case AccessibilityRole::LandmarkComplementary:
            return "LandmarkComplementary"_s;
        case AccessibilityRole::LandmarkContentInfo:
            return "LandmarkContentInfo"_s;
        case AccessibilityRole::LandmarkDocRegion:
            return "LandmarkDocRegion"_s;
        case AccessibilityRole::LandmarkMain:
            return "LandmarkMain"_s;
        case AccessibilityRole::LandmarkNavigation:
            return "LandmarkNavigation"_s;
        case AccessibilityRole::LandmarkRegion:
            return "LandmarkRegion"_s;
        case AccessibilityRole::LandmarkSearch:
            return "LandmarkSearch"_s;
        case AccessibilityRole::Legend:
            return "Legend"_s;
        case AccessibilityRole::LineBreak:
            return "LineBreak"_s;
        case AccessibilityRole::Link:
            return "Link"_s;
        case AccessibilityRole::List:
            return "List"_s;
        case AccessibilityRole::ListBox:
            return "ListBox"_s;
        case AccessibilityRole::ListBoxOption:
            return "ListBoxOption"_s;
        case AccessibilityRole::ListItem:
            return "ListItem"_s;
        case AccessibilityRole::ListMarker:
            return "ListMarker"_s;
        case AccessibilityRole::Mark:
            return "Mark"_s;
        case AccessibilityRole::MathElement:
            return "MathElement"_s;
        case AccessibilityRole::Menu:
            return "Menu"_s;
        case AccessibilityRole::MenuBar:
            return "MenuBar"_s;
        case AccessibilityRole::MenuButton:
            return "MenuButton"_s;
        case AccessibilityRole::MenuItem:
            return "MenuItem"_s;
        case AccessibilityRole::MenuItemCheckbox:
            return "MenuItemCheckbox"_s;
        case AccessibilityRole::MenuItemRadio:
            return "MenuItemRadio"_s;
        case AccessibilityRole::MenuListPopup:
            return "MenuListPopup"_s;
        case AccessibilityRole::MenuListOption:
            return "MenuListOption"_s;
        case AccessibilityRole::Meter:
            return "Meter"_s;
        case AccessibilityRole::Model:
            return "Model"_s;
        case AccessibilityRole::Paragraph:
            return "Paragraph"_s;
        case AccessibilityRole::PopUpButton:
            return "PopUpButton"_s;
        case AccessibilityRole::Pre:
            return "Pre"_s;
        case AccessibilityRole::Presentational:
            return "Presentational"_s;
        case AccessibilityRole::ProgressIndicator:
            return "ProgressIndicator"_s;
        case AccessibilityRole::RadioButton:
            return "RadioButton"_s;
        case AccessibilityRole::RadioGroup:
            return "RadioGroup"_s;
        case AccessibilityRole::RemoteFrame:
            return "RemoteFrame"_s;
        case AccessibilityRole::RowHeader:
            return "RowHeader"_s;
        case AccessibilityRole::Row:
            return "Row"_s;
        case AccessibilityRole::RowGroup:
            return "RowGroup"_s;
        case AccessibilityRole::RubyBase:
            return "RubyBase"_s;
        case AccessibilityRole::RubyBlock:
            return "RubyBlock"_s;
        case AccessibilityRole::RubyInline:
            return "RubyInline"_s;
        case AccessibilityRole::RubyRun:
            return "RubyRun"_s;
        case AccessibilityRole::RubyText:
            return "RubyText"_s;
        case AccessibilityRole::ScrollArea:
            return "ScrollArea"_s;
        case AccessibilityRole::ScrollBar:
            return "ScrollBar"_s;
        case AccessibilityRole::SearchField:
            return "SearchField"_s;
        case AccessibilityRole::Slider:
            return "Slider"_s;
        case AccessibilityRole::SliderThumb:
            return "SliderThumb"_s;
        case AccessibilityRole::SpinButton:
            return "SpinButton"_s;
        case AccessibilityRole::SpinButtonPart:
            return "SpinButtonPart"_s;
        case AccessibilityRole::Splitter:
            return "Splitter"_s;
        case AccessibilityRole::StaticText:
            return "StaticText"_s;
        case AccessibilityRole::Strong:
            return "Strong"_s;
        case AccessibilityRole::Subscript:
            return "Subscript"_s;
        case AccessibilityRole::Suggestion:
            return "Suggestion"_s;
        case AccessibilityRole::Summary:
            return "Summary"_s;
        case AccessibilityRole::Superscript:
            return "Superscript"_s;
        case AccessibilityRole::Switch:
            return "Switch"_s;
        case AccessibilityRole::SVGRoot:
            return "SVGRoot"_s;
        case AccessibilityRole::SVGText:
            return "SVGText"_s;
        case AccessibilityRole::SVGTSpan:
            return "SVGTSpan"_s;
        case AccessibilityRole::SVGTextPath:
            return "SVGTextPath"_s;
        case AccessibilityRole::TabGroup:
            return "TabGroup"_s;
        case AccessibilityRole::TabList:
            return "TabList"_s;
        case AccessibilityRole::TabPanel:
            return "TabPanel"_s;
        case AccessibilityRole::Tab:
            return "Tab"_s;
        case AccessibilityRole::Table:
            return "Table"_s;
        case AccessibilityRole::TableHeaderContainer:
            return "TableHeaderContainer"_s;
        case AccessibilityRole::TextArea:
            return "TextArea"_s;
        case AccessibilityRole::TextGroup:
            return "TextGroup"_s;
        case AccessibilityRole::Term:
            return "Term"_s;
        case AccessibilityRole::Time:
            return "Time"_s;
        case AccessibilityRole::Tree:
            return "Tree"_s;
        case AccessibilityRole::TreeGrid:
            return "TreeGrid"_s;
        case AccessibilityRole::TreeItem:
            return "TreeItem"_s;
        case AccessibilityRole::TextField:
            return "TextField"_s;
        case AccessibilityRole::ToggleButton:
            return "ToggleButton"_s;
        case AccessibilityRole::Toolbar:
            return "Toolbar"_s;
        case AccessibilityRole::Unknown:
            return "Unknown"_s;
        case AccessibilityRole::UserInterfaceTooltip:
            return "UserInterfaceTooltip"_s;
        case AccessibilityRole::Video:
            return "Video"_s;
        case AccessibilityRole::WebApplication:
            return "WebApplication"_s;
        case AccessibilityRole::WebArea:
            return "WebArea"_s;
        case AccessibilityRole::WebCoreLink:
            return "WebCoreLink"_s;
    };
    return "Unknown"_s;
}

static Ref<Inspector::Protocol::Page::AXNode> snapshotForAXObject(RefPtr<AXCoreObject> axObject, Node* nodeToFind)
{
    auto axNode = Inspector::Protocol::Page::AXNode::create()
        .setRole(roleFromObject(axObject))
        .release();
    auto* liveObject = dynamicDowncast<AccessibilityObject>(axObject.get());

    if (liveObject && !liveObject->computedLabel().isEmpty())
        axNode->setName(liveObject->computedLabel());
    if (!axObject->stringValue().isEmpty())
        axNode->setValue(JSON::Value::create(axObject->stringValue()));
    if (liveObject && !liveObject->description().isEmpty())
        axNode->setDescription(liveObject->description());
    if (!axObject->keyShortcuts().isEmpty())
        axNode->setKeyshortcuts(axObject->keyShortcuts());
    if (!axObject->valueDescription().isEmpty())
        axNode->setValuetext(axObject->valueDescription());
    if (!axObject->roleDescription().isEmpty())
        axNode->setRoledescription(axObject->roleDescription());
    if (!axObject->isEnabled())
        axNode->setDisabled(!axObject->isEnabled());
    if (axObject->supportsExpanded())
        axNode->setExpanded(axObject->isExpanded());
    if (axObject->isFocused())
        axNode->setFocused(axObject->isFocused());
    if (axObject->isModalNode())
        axNode->setModal(axObject->isModalNode());
    if (axObject->isMultiSelectable())
        axNode->setMultiselectable(axObject->isMultiSelectable());
    if (liveObject && liveObject->supportsReadOnly() && !axObject->canSetValueAttribute() && axObject->isEnabled())
        axNode->setReadonly(true);
    if (axObject->supportsRequiredAttribute())
        axNode->setRequired(axObject->isRequired());
    if (axObject->isSelected())
        axNode->setSelected(axObject->isSelected());
    if (axObject->supportsChecked()) {
        AccessibilityButtonState checkedState = axObject->checkboxOrRadioValue();
        switch (checkedState) {
            case AccessibilityButtonState::On:
                axNode->setChecked(Inspector::Protocol::Page::AXNode::Checked::True);
                break;
            case AccessibilityButtonState::Off:
                axNode->setChecked(Inspector::Protocol::Page::AXNode::Checked::False);
                break;
            case AccessibilityButtonState::Mixed:
                axNode->setChecked(Inspector::Protocol::Page::AXNode::Checked::Mixed);
                break;
        }
    }
    unsigned level = axObject->hierarchicalLevel() ? axObject->hierarchicalLevel() : axObject->headingLevel();
    if (level)
        axNode->setLevel(level);
    if (axObject->minValueForRange() != 0)
        axNode->setValuemin(axObject->minValueForRange());
    if (axObject->maxValueForRange() != 0)
        axNode->setValuemax(axObject->maxValueForRange());
    if (liveObject && liveObject->supportsAutoComplete())
        axNode->setAutocomplete(axObject->autoCompleteValue());
    if (axObject->hasPopup())
        axNode->setHaspopup(axObject->popupValue());

    String invalidValue = axObject->invalidStatus();
    if (invalidValue != "false"_s) {
        if (invalidValue == "grammar"_s)
            axNode->setInvalid(Inspector::Protocol::Page::AXNode::Invalid::Grammar);
        else if (invalidValue == "spelling"_s)
            axNode->setInvalid(Inspector::Protocol::Page::AXNode::Invalid::Spelling);
        else // Future versions of ARIA may allow additional truthy values. Ex. format, order, or size.
            axNode->setInvalid(Inspector::Protocol::Page::AXNode::Invalid::True);
    }
    switch (axObject->orientation()) {
        case AccessibilityOrientation::Undefined:
            break;
        case AccessibilityOrientation::Vertical:
            axNode->setOrientation("vertical"_s);
            break;
        case AccessibilityOrientation::Horizontal:
            axNode->setOrientation("horizontal"_s);
            break;
    }

    if (axObject->isKeyboardFocusable())
        axNode->setFocusable(axObject->isKeyboardFocusable());

    if (nodeToFind && axObject->node() == nodeToFind)
        axNode->setFound(true);

    if (!axObject->children().isEmpty()) {
        Ref<JSON::ArrayOf<Inspector::Protocol::Page::AXNode>> children = JSON::ArrayOf<Inspector::Protocol::Page::AXNode>::create();
        for (auto& childObject : axObject->children())
            children->addItem(snapshotForAXObject(childObject, nodeToFind));
        axNode->setChildren(WTFMove(children));
    }
    return axNode;
}


Protocol::ErrorStringOr<Ref<Protocol::Page::AXNode>> InspectorPageAgent::accessibilitySnapshot(const String& objectId)
{
    if (!WebCore::AXObjectCache::accessibilityEnabled())
        WebCore::AXObjectCache::enableAccessibility();

    auto* localMainFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    if (!localMainFrame)
        return makeUnexpected("No local main frame"_s);

    RefPtr document = localMainFrame->document();
    if (!document)
        return makeUnexpected("No document for main frame"_s);

    AXObjectCache* axObjectCache = document->axObjectCache();
    if (!axObjectCache)
        return makeUnexpected("No AXObjectCache for main document"_s);

    AXCoreObject* axObject = axObjectCache->rootObject();
    if (!axObject)
        return makeUnexpected("No AXObject for main document"_s);

    Node* node = nullptr;
    if (!objectId.isEmpty()) {
        InspectorDOMAgent* domAgent = m_instrumentingAgents.persistentDOMAgent();
        ASSERT(domAgent);
        node = domAgent->nodeForObjectId(objectId);
        if (!node)
            return makeUnexpected("No Node for objectId"_s);
    }

    m_doingAccessibilitySnapshot = true;
    Ref<Inspector::Protocol::Page::AXNode> axNode = snapshotForAXObject(RefPtr { axObject }, node);
    m_doingAccessibilitySnapshot = false;
    return axNode;
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setInterceptFileChooserDialog(bool enabled)
{
    m_interceptFileChooserDialog = enabled;
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setDefaultBackgroundColorOverride(RefPtr<JSON::Object>&& color)
{
    auto* localFrame = dynamicDowncast<LocalFrame>(m_inspectedPage.mainFrame());
    LocalFrameView* view = localFrame ? localFrame->view() : nullptr;
    if (!view)
        return makeUnexpected("Internal error: No frame view to set color two"_s);

    if (!color) {
        view->updateBackgroundRecursively(std::optional<Color>());
        return { };
    }

    view->updateBackgroundRecursively(InspectorDOMAgent::parseColor(WTFMove(color)));
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::createUserWorld(const String& name)
{
    if (createdUserWorlds().contains(name))
        return makeUnexpected("World with the given name already exists"_s);

    Ref<DOMWrapperWorld> world = ScriptController::createWorld(name, ScriptController::WorldType::User);
    ensureUserWorldsExistInAllFrames({world.ptr()});
    createdUserWorlds().set(name, WTFMove(world));
    return { };
}

void InspectorPageAgent::ensureUserWorldsExistInAllFrames(const Vector<DOMWrapperWorld*>& worlds)
{
    for (Frame* frame = &m_inspectedPage.mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        for (auto* world : worlds)
            localFrame->windowProxy().jsWindowProxy(*world)->window();
    }
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setBypassCSP(bool enabled)
{
    m_bypassCSP = enabled;
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::crash()
{
    CRASH();
    return { };
}

Protocol::ErrorStringOr<void> InspectorPageAgent::setOrientationOverride(std::optional<int>&& angle)
{
#if ENABLE(ORIENTATION_EVENTS)
    m_inspectedPage.setOverrideOrientation(WTFMove(angle));
    return { };
#else
    UNUSED_PARAM(angle);
    return makeUnexpected("Orientation events are disabled in this build");
#endif
}

Protocol::ErrorStringOr<void> InspectorPageAgent::updateScrollingState()
{
    auto* scrollingCoordinator = m_inspectedPage.scrollingCoordinator();
    if (!scrollingCoordinator)
        return {};
    scrollingCoordinator->commitTreeStateIfNeeded();
    return {};
}

} // namespace WebCore
