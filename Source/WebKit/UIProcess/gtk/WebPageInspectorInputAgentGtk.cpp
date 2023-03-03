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
#include "WebPageInspectorInputAgent.h"

#include "KeyBindingTranslator.h"
#include "NativeWebKeyboardEvent.h"
#include "WebPageProxy.h"
#include <WebCore/PlatformKeyboardEvent.h>

namespace WebKit {

static Vector<String> commandsForKeyEvent(GdkEventType type, unsigned keyVal, unsigned state)
{
    ASSERT(type == GDK_KEY_PRESS || type == GDK_KEY_RELEASE);

    GUniquePtr<GdkEvent> event(gdk_event_new(type));
    event->key.keyval = keyVal;
    event->key.time = GDK_CURRENT_TIME;
    event->key.state = state;
    // When synthesizing an event, an invalid hardware_keycode value can cause it to be badly processed by GTK+.
    GUniqueOutPtr<GdkKeymapKey> keys;
    int keysCount;
    if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_default(), keyVal, &keys.outPtr(), &keysCount) && keysCount)
        event->key.hardware_keycode = keys.get()[0].keycode;
    return KeyBindingTranslator().commandsForKeyEvent(&event->key);
}

static unsigned modifiersToEventState(OptionSet<WebEventModifier> modifiers)
{
    unsigned state = 0;
    if (modifiers.contains(WebEventModifier::ControlKey))
        state |= GDK_CONTROL_MASK;
    if (modifiers.contains(WebEventModifier::ShiftKey))
        state |= GDK_SHIFT_MASK;
    if (modifiers.contains(WebEventModifier::AltKey))
        state |= GDK_META_MASK;
    if (modifiers.contains(WebEventModifier::CapsLockKey))
        state |= GDK_LOCK_MASK;
    return state;
}

void WebPageInspectorInputAgent::platformDispatchKeyEvent(WebEventType type, const String& text, const String& unmodifiedText, const String& key, const String& code, const String& keyIdentifier, int windowsVirtualKeyCode, int nativeVirtualKeyCode, bool isAutoRepeat, bool isKeypad, bool isSystemKey, OptionSet<WebEventModifier> modifiers, Vector<String>& macCommands, WallTime timestamp)
{
    Vector<String> commands;
    const guint keyVal = WebCore::PlatformKeyboardEvent::gdkKeyCodeForWindowsKeyCode(windowsVirtualKeyCode);
    if (keyVal) {
        GdkEventType event = GDK_NOTHING;
        switch (type)
        {
        case WebEventType::KeyDown:
            event = GDK_KEY_PRESS;
            break;
        case WebEventType::KeyUp:
            event = GDK_KEY_RELEASE;
            break;
        default:
            fprintf(stderr, "Unsupported event type = %d\n", type);
            break;
        }
        unsigned state = modifiersToEventState(modifiers);
        commands = commandsForKeyEvent(event, keyVal, state);
    }
    NativeWebKeyboardEvent event(
        type,
        text,
        unmodifiedText,
        key,
        code,
        keyIdentifier,
        windowsVirtualKeyCode,
        nativeVirtualKeyCode,
        isAutoRepeat,
        isKeypad,
        isSystemKey,
        modifiers,
        timestamp,
        WTFMove(commands));
    m_page.handleKeyboardEvent(event);
}

} // namespace WebKit
