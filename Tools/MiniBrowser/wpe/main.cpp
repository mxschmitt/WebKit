/*
 * Copyright (C) 2018 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cmakeconfig.h"

#include "BuildRevision.h"
#include <WPEToolingBackends/HeadlessViewBackend.h>
#include <WPEToolingBackends/WindowViewBackend.h>
#include <memory>
#include <wpe/webkit.h>

#if USE_ATK
#include <atk/atk.h>
#endif

#if !USE_GSTREAMER_FULL && (ENABLE_WEB_AUDIO || ENABLE_VIDEO)
#include <gst/gst.h>
#endif

static const char** uriArguments;
static const char** ignoreHosts;
static gboolean headlessMode;
static gboolean privateMode;
static gboolean automationMode;
static gboolean ignoreTLSErrors;
static gboolean inspectorPipe;
static gboolean noStartupWindow;
static const char* userDataDir;
static const char* contentFilter;
static const char* cookiesFile;
static const char* cookiesPolicy;
static const char* proxy;
const char* bgColor;
static char* timeZone;
static const char* featureList = nullptr;
static gboolean enableITP;
static gboolean printVersion;
static GHashTable* openViews;
#if ENABLE_WPE_PLATFORM
static gboolean useWPEPlatformAPI;
#endif

static const GOptionEntry commandLineOptions[] =
{
    { "headless", 'h', 0, G_OPTION_ARG_NONE, &headlessMode, "Run in headless mode", nullptr },
    { "private", 'p', 0, G_OPTION_ARG_NONE, &privateMode, "Run in private browsing mode", nullptr },
    { "automation", 0, 0, G_OPTION_ARG_NONE, &automationMode, "Run in automation mode", nullptr },
    { "cookies-file", 'c', 0, G_OPTION_ARG_FILENAME, &cookiesFile, "Persistent cookie storage database file", "FILE" },
    { "cookies-policy", 0, 0, G_OPTION_ARG_STRING, &cookiesPolicy, "Cookies accept policy (always, never, no-third-party). Default: no-third-party", "POLICY" },
    { "proxy", 0, 0, G_OPTION_ARG_STRING, &proxy, "Set proxy", "PROXY" },
    { "ignore-host", 0, 0, G_OPTION_ARG_STRING_ARRAY, &ignoreHosts, "Set proxy ignore hosts", "HOSTS" },
    { "ignore-tls-errors", 0, 0, G_OPTION_ARG_NONE, &ignoreTLSErrors, "Ignore TLS errors", nullptr },
    { "content-filter", 0, 0, G_OPTION_ARG_FILENAME, &contentFilter, "JSON with content filtering rules", "FILE" },
    { "bg-color", 0, 0, G_OPTION_ARG_STRING, &bgColor, "Window background color. Default: white", "COLOR" },
    { "enable-itp", 0, 0, G_OPTION_ARG_NONE, &enableITP, "Enable Intelligent Tracking Prevention (ITP)", nullptr },
    { "time-zone", 't', 0, G_OPTION_ARG_STRING, &timeZone, "Set time zone", "TIMEZONE" },
    { "features", 'F', 0, G_OPTION_ARG_STRING, &featureList, "Enable or disable WebKit features (hint: pass 'help' for a list)", "FEATURE-LIST" },
#if ENABLE_WPE_PLATFORM
    { "use-wpe-platform-api", 0, 0, G_OPTION_ARG_NONE, &useWPEPlatformAPI, "Use the WPE platform API", nullptr },
#endif
    { "version", 'v', 0, G_OPTION_ARG_NONE, &printVersion, "Print the WPE version", nullptr },
    { "inspector-pipe", 'v', 0, G_OPTION_ARG_NONE, &inspectorPipe, "Expose remote debugging protocol over pipe", nullptr },
    { "user-data-dir", 0, 0, G_OPTION_ARG_STRING, &userDataDir, "Default profile persistence folder location", "FILE" },
    { "no-startup-window", 0, 0, G_OPTION_ARG_NONE, &noStartupWindow, "Do not open default page", nullptr },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &uriArguments, nullptr, "[URL]" },
    { nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr }
};

class InputClient final : public WPEToolingBackends::ViewBackend::InputClient {
public:
    InputClient(GApplication* application, WebKitWebView* webView)
        : m_application(application)
        , m_webView(webView)
    {
    }

    bool dispatchKeyboardEvent(struct wpe_input_keyboard_event* event) override
    {
        if (!event->pressed)
            return false;

        if (event->modifiers & wpe_input_keyboard_modifier_control && event->key_code == WPE_KEY_q) {
            g_application_quit(m_application);
            return true;
        }

        if (event->modifiers & wpe_input_keyboard_modifier_alt) {
            if ((event->key_code == WPE_KEY_Left || event->key_code == WPE_KEY_KP_Left) && webkit_web_view_can_go_back(m_webView)) {
                webkit_web_view_go_back(m_webView);
                return true;
            }

            if ((event->key_code == WPE_KEY_Right || event->key_code == WPE_KEY_KP_Right) && webkit_web_view_can_go_forward(m_webView)) {
                webkit_web_view_go_forward(m_webView);
                return true;
            }
        }

        return false;
    }

private:
    GApplication* m_application { nullptr };
    WebKitWebView* m_webView { nullptr };
};

#if ENABLE_WPE_PLATFORM
static gboolean wpeViewEventCallback(WPEView* view, WPEEvent* event, WebKitWebView* webView)
{
    if (wpe_event_get_event_type(event) != WPE_EVENT_KEYBOARD_KEY_DOWN)
        return FALSE;

    auto modifiers = wpe_event_get_modifiers(event);
    auto keyval = wpe_event_keyboard_get_keyval(event);

    if (modifiers & WPE_MODIFIER_KEYBOARD_CONTROL && keyval == WPE_KEY_q) {
        g_application_quit(g_application_get_default());
        return TRUE;
    }

    if (modifiers & WPE_MODIFIER_KEYBOARD_ALT) {
        if ((keyval == WPE_KEY_Left || keyval == WPE_KEY_KP_Left) && webkit_web_view_can_go_back(webView)) {
            webkit_web_view_go_back(webView);
            return TRUE;
        }

        if ((keyval == WPE_KEY_Right || keyval == WPE_KEY_KP_Right) && webkit_web_view_can_go_forward(webView)) {
            webkit_web_view_go_forward(webView);
            return TRUE;
        }

        if (keyval == WPE_KEY_Up) {
            if (wpe_view_get_state(view) & WPE_VIEW_STATE_MAXIMIZED)
                wpe_view_unmaximize(view);
            else
                wpe_view_maximize(view);
            return TRUE;
        }
    }

    if (keyval == WPE_KEY_F11) {
        if (wpe_view_get_state(view) & WPE_VIEW_STATE_FULLSCREEN)
            wpe_view_unfullscreen(view);
        else
            wpe_view_fullscreen(view);
        return TRUE;
    }

    return FALSE;
}
#endif

static WebKitWebView* createWebViewForAutomationCallback(WebKitAutomationSession*, WebKitWebView* view)
{
    return view;
}

static void automationStartedCallback(WebKitWebContext*, WebKitAutomationSession* session, WebKitWebView* view)
{
    auto* info = webkit_application_info_new();
    webkit_application_info_set_version(info, WEBKIT_MAJOR_VERSION, WEBKIT_MINOR_VERSION, WEBKIT_MICRO_VERSION);
    webkit_automation_session_set_application_info(session, info);
    webkit_application_info_unref(info);

    g_signal_connect(session, "create-web-view", G_CALLBACK(createWebViewForAutomationCallback), view);
}

static gboolean decidePermissionRequest(WebKitWebView *, WebKitPermissionRequest *request, gpointer)
{
    g_print("Accepting %s request\n", G_OBJECT_TYPE_NAME(request));
    webkit_permission_request_allow(request);

    return TRUE;
}

static std::unique_ptr<WPEToolingBackends::ViewBackend> createViewBackend(uint32_t width, uint32_t height)
{
#if ENABLE_WPE_PLATFORM
    if (useWPEPlatformAPI)
        return nullptr;
#endif

    if (headlessMode)
        return std::make_unique<WPEToolingBackends::HeadlessViewBackend>(width, height);
    return std::make_unique<WPEToolingBackends::WindowViewBackend>(width, height);
}

struct FilterSaveData {
    GMainLoop* mainLoop { nullptr };
    WebKitUserContentFilter* filter { nullptr };
    GError* error { nullptr };
};

static void filterSavedCallback(WebKitUserContentFilterStore *store, GAsyncResult *result, FilterSaveData *data)
{
    data->filter = webkit_user_content_filter_store_save_finish(store, result, &data->error);
    g_main_loop_quit(data->mainLoop);
}

static gboolean webViewLoadFailed()
{
    return TRUE;
}

static void webViewClose(WebKitWebView* webView, gpointer user_data)
{
    // Hash table key delete func takes care of unref'ing the view
    g_hash_table_remove(openViews, webView);
    if (!g_hash_table_size(openViews) && user_data)
        g_application_quit(G_APPLICATION(user_data));
}

static gboolean scriptDialog(WebKitWebView*, WebKitScriptDialog* dialog, gpointer)
{
    if (inspectorPipe)
        webkit_script_dialog_ref(dialog);
    return TRUE;
}

static gboolean scriptDialogHandled(WebKitWebView*, WebKitScriptDialog* dialog, gpointer)
{
    if (inspectorPipe)
        webkit_script_dialog_unref(dialog);
    return TRUE;
}

static gboolean webViewDecidePolicy(WebKitWebView *webView, WebKitPolicyDecision *decision, WebKitPolicyDecisionType decisionType, gpointer);

static WebKitWebView* createWebView(WebKitWebView* webView, WebKitNavigationAction*, gpointer user_data);

static WebKitWebView* createWebViewImpl(WebKitWebView* webView, WebKitWebContext *webContext, gpointer user_data)
{

    auto backend = createViewBackend(1280, 720);
    WebKitWebViewBackend* viewBackend = nullptr;
    if (backend) {
        struct wpe_view_backend* wpeBackend = backend->backend();
        if (!wpeBackend)
            return nullptr;

        viewBackend = webkit_web_view_backend_new(wpeBackend,
            [](gpointer data) {
                delete static_cast<WPEToolingBackends::ViewBackend*>(data);
            }, backend.release());
    }

// Playwright begin
    if (headlessMode) {
        webkit_web_view_backend_set_screenshot_callback(viewBackend,
            [](gpointer data) {
                return static_cast<WPEToolingBackends::HeadlessViewBackend*>(data)->snapshot();
            });
    }
// Playwright end
    WebKitWebView* newWebView;
    if (webView) {
        newWebView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "backend", viewBackend,
            "related-view", webView,
            nullptr));
    } else {
        newWebView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "backend", viewBackend,
            "web-context", webContext,
            "is-controlled-by-automation", TRUE,
            nullptr));
    }

    g_signal_connect(newWebView, "create", G_CALLBACK(createWebView), user_data);
    g_signal_connect(newWebView, "close", G_CALLBACK(webViewClose), user_data);

    g_hash_table_add(openViews, newWebView);

    g_signal_connect(newWebView, "load-failed", G_CALLBACK(webViewLoadFailed), nullptr);
    g_signal_connect(newWebView, "script-dialog", G_CALLBACK(scriptDialog), nullptr);
    g_signal_connect(newWebView, "script-dialog-handled", G_CALLBACK(scriptDialogHandled), nullptr);
    g_signal_connect(newWebView, "decide-policy", G_CALLBACK(webViewDecidePolicy), nullptr);
    return newWebView;
}

static WebKitFeature* findFeature(WebKitFeatureList* featureList, const char* identifier)
{
    for (gsize i = 0; i < webkit_feature_list_get_length(featureList); i++) {
        WebKitFeature* feature = webkit_feature_list_get(featureList, i);
        if (!g_ascii_strcasecmp(identifier, webkit_feature_get_identifier(feature)))
            return feature;
    }
    return nullptr;
}

static WebKitWebView* createWebView(WebKitWebView* webView, WebKitNavigationAction*, gpointer user_data)
{
    return createWebViewImpl(webView, nullptr, user_data);
}

static gboolean webViewDecidePolicy(WebKitWebView *webView, WebKitPolicyDecision *decision, WebKitPolicyDecisionType decisionType, gpointer user_data)
{
    if (decisionType == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *responseDecision = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (!webkit_response_policy_decision_is_main_frame_main_resource(responseDecision))
            return FALSE;

        const gchar* mimeType = webkit_uri_response_get_mime_type(webkit_response_policy_decision_get_response(responseDecision));
        if (!webkit_response_policy_decision_is_mime_type_supported(responseDecision) && mimeType && mimeType[0] != '\0') {
            webkit_policy_decision_download(decision);
            return TRUE;
        }

        webkit_policy_decision_use(decision);
        return TRUE;
    }

    if (decisionType != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;

    WebKitNavigationAction *navigationAction = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    if (webkit_navigation_action_get_navigation_type(navigationAction) != WEBKIT_NAVIGATION_TYPE_LINK_CLICKED)
        return FALSE;

    guint modifiers = webkit_navigation_action_get_modifiers(navigationAction);
    if (webkit_navigation_action_get_mouse_button(navigationAction) != 2 /* GDK_BUTTON_MIDDLE */ &&
        (webkit_navigation_action_get_mouse_button(navigationAction) != 1 /* GDK_BUTTON_PRIMARY */ || (modifiers & (wpe_input_keyboard_modifier_control | wpe_input_keyboard_modifier_shift)) == 0))
        return FALSE;

    /* Open a new tab if link clicked with the middle button, shift+click or ctrl+click. */
    WebKitWebView* newWebView = createWebViewImpl(nullptr, webkit_web_view_get_context(webView), user_data);
    webkit_web_view_load_request(newWebView, webkit_navigation_action_get_request(navigationAction));

    webkit_policy_decision_ignore(decision);
    return TRUE;
}

static WebKitWebContext *persistentWebContext = NULL;

static WebKitWebView* createNewPage(WebKitBrowserInspector*, WebKitWebContext *webContext)
{
    if (!webContext)
        webContext = persistentWebContext;
    WebKitWebView* webView = createWebViewImpl(nullptr, webContext, nullptr);
    webkit_web_view_load_uri(webView, "about:blank");
    return webView;
}

static void quitBroserApplication(WebKitBrowserInspector* browser_inspector, gpointer data)
{
    GApplication* application = static_cast<GApplication*>(data);
    g_application_quit(application);
}

static void configureBrowserInspector(GApplication* application)
{
    WebKitBrowserInspector* browserInspector = webkit_browser_inspector_get_default();
    g_signal_connect(browserInspector, "create-new-page", G_CALLBACK(createNewPage), NULL);
    g_signal_connect(browserInspector, "quit-application", G_CALLBACK(quitBroserApplication), application);
    webkit_browser_inspector_initialize_pipe(proxy, ignoreHosts);
}

static void activate(GApplication* application, WPEToolingBackends::ViewBackend* backend)
{
    g_application_hold(application);
    if (noStartupWindow)
        return;
#if ENABLE_2022_GLIB_API
    WebKitNetworkSession* networkSession = nullptr;
    if (!automationMode) {
        if (userDataDir) {
            networkSession = webkit_network_session_new(userDataDir, userDataDir);
            cookiesFile = g_build_filename(userDataDir, "cookies.txt", nullptr);
        } else if (inspectorPipe || privateMode || automationMode) {
            networkSession = webkit_network_session_new_ephemeral();
        } else {
            networkSession = webkit_network_session_new(nullptr, nullptr);
        }        
        webkit_network_session_set_itp_enabled(networkSession, enableITP);

        if (proxy) {
            auto* webkitProxySettings = webkit_network_proxy_settings_new(proxy, ignoreHosts);
            webkit_network_session_set_proxy_settings(networkSession, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, webkitProxySettings);
            webkit_network_proxy_settings_free(webkitProxySettings);
        }

        if (ignoreTLSErrors)
            webkit_network_session_set_tls_errors_policy(networkSession, WEBKIT_TLS_ERRORS_POLICY_IGNORE);

        if (cookiesPolicy) {
            auto* cookieManager = webkit_network_session_get_cookie_manager(networkSession);
            auto* enumClass = static_cast<GEnumClass*>(g_type_class_ref(WEBKIT_TYPE_COOKIE_ACCEPT_POLICY));
            GEnumValue* enumValue = g_enum_get_value_by_nick(enumClass, cookiesPolicy);
            if (enumValue)
                webkit_cookie_manager_set_accept_policy(cookieManager, static_cast<WebKitCookieAcceptPolicy>(enumValue->value));
            g_type_class_unref(enumClass);
        }

        if (cookiesFile && !webkit_network_session_is_ephemeral(networkSession)) {
            auto* cookieManager = webkit_network_session_get_cookie_manager(networkSession);
            auto storageType = g_str_has_suffix(cookiesFile, ".txt") ? WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT : WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE;
            webkit_cookie_manager_set_persistent_storage(cookieManager, cookiesFile, storageType);
        }
    }
    auto* webContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT, "time-zone-override", timeZone, nullptr));
    webkit_web_context_set_network_session_for_automation(webContext, networkSession);
#else
    WebKitWebsiteDataManager *manager;
    if (userDataDir) {
        manager = webkit_website_data_manager_new("base-data-directory", userDataDir, "base-cache-directory", userDataDir, NULL);
        cookiesFile = g_build_filename(userDataDir, "cookies.txt", NULL);
    } else if (inspectorPipe || privateMode || automationMode) {
        manager = webkit_website_data_manager_new_ephemeral();
    } else {
        manager = webkit_website_data_manager_new(NULL);
    }
    webkit_website_data_manager_set_itp_enabled(manager, enableITP);

    if (proxy) {
        auto* webkitProxySettings = webkit_network_proxy_settings_new(proxy, ignoreHosts);
        webkit_website_data_manager_set_network_proxy_settings(manager, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, webkitProxySettings);
        webkit_network_proxy_settings_free(webkitProxySettings);
    }

    if (ignoreTLSErrors)
        webkit_website_data_manager_set_tls_errors_policy(manager, WEBKIT_TLS_ERRORS_POLICY_IGNORE);

    auto* webContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT, "website-data-manager", manager, "time-zone-override", timeZone, nullptr));
    g_object_unref(manager);

    if (cookiesPolicy) {
        auto* cookieManager = webkit_web_context_get_cookie_manager(webContext);
        auto* enumClass = static_cast<GEnumClass*>(g_type_class_ref(WEBKIT_TYPE_COOKIE_ACCEPT_POLICY));
        GEnumValue* enumValue = g_enum_get_value_by_nick(enumClass, cookiesPolicy);
        if (enumValue)
            webkit_cookie_manager_set_accept_policy(cookieManager, static_cast<WebKitCookieAcceptPolicy>(enumValue->value));
        g_type_class_unref(enumClass);
    }

    if (cookiesFile && !webkit_web_context_is_ephemeral(webContext)) {
        auto* cookieManager = webkit_web_context_get_cookie_manager(webContext);
        auto storageType = g_str_has_suffix(cookiesFile, ".txt") ? WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT : WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE;
        webkit_cookie_manager_set_persistent_storage(cookieManager, cookiesFile, storageType);
    }
#endif

    persistentWebContext = webContext;
    WebKitUserContentManager* userContentManager = nullptr;
    if (contentFilter) {
        GFile* contentFilterFile = g_file_new_for_commandline_arg(contentFilter);

        FilterSaveData saveData = { nullptr, };
        gchar* filtersPath = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), "filters", nullptr);
        WebKitUserContentFilterStore* store = webkit_user_content_filter_store_new(filtersPath);
        g_free(filtersPath);

        webkit_user_content_filter_store_save_from_file(store, "WPEMiniBrowserFilter", contentFilterFile, nullptr, (GAsyncReadyCallback)filterSavedCallback, &saveData);
        saveData.mainLoop = g_main_loop_new(nullptr, FALSE);
        g_main_loop_run(saveData.mainLoop);
        g_object_unref(store);

        if (saveData.filter) {
            userContentManager = webkit_user_content_manager_new();
            webkit_user_content_manager_add_filter(userContentManager, saveData.filter);
        } else
            g_printerr("Cannot save filter '%s': %s\n", contentFilter, saveData.error->message);

        g_clear_pointer(&saveData.error, g_error_free);
        g_clear_pointer(&saveData.filter, webkit_user_content_filter_unref);
        g_main_loop_unref(saveData.mainLoop);
        g_object_unref(contentFilterFile);
    }

    auto* settings = webkit_settings_new_with_settings(
        "enable-developer-extras", TRUE,
        "enable-webgl", TRUE,
        "enable-media-stream", TRUE,
        "enable-webrtc", TRUE,
        "enable-encrypted-media", TRUE,
        nullptr);

    if (featureList) {
        g_autoptr(WebKitFeatureList) features = webkit_settings_get_all_features();
        g_auto(GStrv) items = g_strsplit(featureList, ",", -1);
        for (gsize i = 0; items[i]; i++) {
            char* item = g_strchomp(items[i]);
            gboolean enabled = TRUE;
            switch (item[0]) {
            case '!':
            case '-':
                enabled = FALSE;
                [[fallthrough]];
            case '+':
                item++;
                [[fallthrough]];
            default:
                break;
            }

            if (item[0] == '\0') {
                g_printerr("Empty feature name specified, skipped.");
                continue;
            }

            if (auto* feature = findFeature(features, item))
                webkit_settings_set_feature_enabled(settings, feature, enabled);
            else
                g_printerr("Feature '%s' is not available.", item);
        }
    }

    auto* viewBackend = backend ? webkit_web_view_backend_new(backend->backend(), [](gpointer data) {
        delete static_cast<WPEToolingBackends::ViewBackend*>(data);
    }, backend) : nullptr;

    auto* defaultWebsitePolicies = webkit_website_policies_new_with_policies(
        "autoplay", WEBKIT_AUTOPLAY_ALLOW,
        nullptr);

// Playwright begin
    if (headlessMode) {
        webkit_web_view_backend_set_screenshot_callback(viewBackend,
            [](gpointer data) {
                return static_cast<WPEToolingBackends::HeadlessViewBackend*>(data)->snapshot();
            });
    }
// Playwright end

    auto* webView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "backend", viewBackend,
        "web-context", webContext,
#if ENABLE_2022_GLIB_API
        "network-session", networkSession,
#endif
        "settings", settings,
        "user-content-manager", userContentManager,
        "is-controlled-by-automation", automationMode,
        "website-policies", defaultWebsitePolicies,
        nullptr));
    g_object_unref(settings);
    g_object_unref(defaultWebsitePolicies);

    if (backend) {
        backend->setInputClient(std::make_unique<InputClient>(application, webView));
#if USE_ATK
        auto* accessible = wpe_view_backend_dispatch_get_accessible(backend->backend());
        if (ATK_IS_OBJECT(accessible))
            backend->setAccessibleChild(ATK_OBJECT(accessible));
#endif
    }

#if ENABLE_WPE_PLATFORM
    if (auto* wpeView = webkit_web_view_get_wpe_view(webView))
        g_signal_connect(wpeView, "event", G_CALLBACK(wpeViewEventCallback), webView);
#endif

    webkit_web_context_set_automation_allowed(webContext, automationMode);
    g_signal_connect(webContext, "automation-started", G_CALLBACK(automationStartedCallback), webView);
    g_signal_connect(webView, "permission-request", G_CALLBACK(decidePermissionRequest), nullptr);
    g_signal_connect(webView, "create", G_CALLBACK(createWebView), application);
    g_signal_connect(webView, "close", G_CALLBACK(webViewClose), application);
    g_hash_table_add(openViews, webView);

    WebKitColor color;
    if (bgColor && webkit_color_parse(&color, bgColor))
        webkit_web_view_set_background_color(webView, &color);

    if (uriArguments) {
        // Playwright: avoid weird url transformation like http://trac.webkit.org/r240840
        webkit_web_view_load_uri(webView, uriArguments[0]);
    } else if (automationMode || inspectorPipe)
        webkit_web_view_load_uri(webView, "about:blank");
    else
        webkit_web_view_load_uri(webView, "https://wpewebkit.org");

    g_object_unref(webContext);
#if ENABLE_2022_GLIB_API
    g_clear_object(&networkSession);
#endif
}

int main(int argc, char *argv[])
{
#if ENABLE_DEVELOPER_MODE
    g_setenv("WEBKIT_INJECTED_BUNDLE_PATH", WEBKIT_INJECTED_BUNDLE_PATH, FALSE);
#endif

    GOptionContext* context = g_option_context_new(nullptr);
    g_option_context_add_main_entries(context, commandLineOptions, nullptr);

#if !USE_GSTREAMER_FULL && (ENABLE_WEB_AUDIO || ENABLE_VIDEO)
    g_option_context_add_group(context, gst_init_get_option_group());
#endif

    GError* error = nullptr;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Cannot parse arguments: %s\n", error->message);
        g_error_free(error);
        g_option_context_free(context);

        return 1;
    }
    g_option_context_free(context);

    if (printVersion) {
        g_print("WPE WebKit %u.%u.%u",
            webkit_get_major_version(),
            webkit_get_minor_version(),
            webkit_get_micro_version());
        if (g_strcmp0(BUILD_REVISION, "tarball"))
            g_print(" (%s)", BUILD_REVISION);
        g_print("\n");
        return 0;
    }

    if (!g_strcmp0(featureList, "help")) {
        g_print("Multiple feature names may be specified separated by commas. No prefix or '+' enable\n"
                "features, prefixes '-' and '!' disable features. Names are case-insensitive. Example:\n"
                "\n    %s --features='!DirPseudo,+WebAnimationsCustomEffects,webgl'\n\n"
                "Available features (+/- = enabled/disabled by default):\n\n", g_get_prgname());
        g_autoptr(GEnumClass) statusEnum = static_cast<GEnumClass*>(g_type_class_ref(WEBKIT_TYPE_FEATURE_STATUS));
        g_autoptr(WebKitFeatureList) features = webkit_settings_get_all_features();
        for (gsize i = 0; i < webkit_feature_list_get_length(features); i++) {
            WebKitFeature* feature = webkit_feature_list_get(features, i);
            g_print("  %c %s (%s)",
                    webkit_feature_get_default_value(feature) ? '+' : '-',
                    webkit_feature_get_identifier(feature),
                    g_enum_get_value(statusEnum, webkit_feature_get_status(feature))->value_nick);
            if (webkit_feature_get_name(feature))
                g_print(": %s", webkit_feature_get_name(feature));
            g_print("\n");
        }
        return 0;
    }

    auto backend = createViewBackend(1280, 720);
    if (backend) {
        struct wpe_view_backend* wpeBackend = backend->backend();
        if (!wpeBackend) {
            g_warning("Failed to create WPE view backend");
            return 1;
        }
    }

    openViews = g_hash_table_new_full(nullptr, nullptr, g_object_unref, nullptr);

    GApplication* application = g_application_new("org.wpewebkit.MiniBrowser", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(application, "activate", G_CALLBACK(activate), backend.release());

    if (inspectorPipe)
        configureBrowserInspector(application);

    g_application_run(application, 0, nullptr);
    g_object_unref(application);

    g_hash_table_destroy(openViews);

    return 0;
}
