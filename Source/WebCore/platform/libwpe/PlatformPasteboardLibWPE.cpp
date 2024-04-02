/*
 * Copyright (C) 2015 Igalia S.L.
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
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformPasteboard.h"

#if USE(LIBWPE)

#include "Pasteboard.h"
#include <wpe/wpe.h>
#include <wtf/Assertions.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

static HashMap<String, String>& sharedPasteboard()
{
    static NeverDestroyed<HashMap<String, String>> pasteboard;
    return pasteboard.get();
}

PlatformPasteboard::PlatformPasteboard(const String&)
    : m_pasteboard(wpe_pasteboard_get_singleton())
{
    ASSERT(m_pasteboard);
}

PlatformPasteboard::PlatformPasteboard()
    : m_pasteboard(wpe_pasteboard_get_singleton())
{
    ASSERT(m_pasteboard);
}

void PlatformPasteboard::performAsDataOwner(DataOwnerType, Function<void()>&& actions)
{
    actions();
}

void PlatformPasteboard::getTypes(Vector<String>& types) const
{
<<<<<<< HEAD
    struct wpe_pasteboard_string_vector pasteboardTypes = { nullptr, 0 };
    wpe_pasteboard_get_types(m_pasteboard, &pasteboardTypes);

    for (unsigned i = 0; i < pasteboardTypes.length; ++i) {
        auto& typeString = pasteboardTypes.strings[i];
        types.append(String({ typeString.data, typeString.length }));
    }

    wpe_pasteboard_string_vector_free(&pasteboardTypes);
||||||| parent of b0fa7c52d79a (chore(webkit): bootstrap build #1994)
    struct wpe_pasteboard_string_vector pasteboardTypes = { nullptr, 0 };
    wpe_pasteboard_get_types(m_pasteboard, &pasteboardTypes);

    for (unsigned i = 0; i < pasteboardTypes.length; ++i) {
        auto& typeString = pasteboardTypes.strings[i];
        types.append(String(typeString.data, typeString.length));
    }

    wpe_pasteboard_string_vector_free(&pasteboardTypes);
=======
    for (const auto& type : sharedPasteboard().keys())
        types.append(type);
>>>>>>> b0fa7c52d79a (chore(webkit): bootstrap build #1994)
}

String PlatformPasteboard::readString(size_t, const String& type) const
{
<<<<<<< HEAD
    struct wpe_pasteboard_string string = { nullptr, 0 };
    wpe_pasteboard_get_string(m_pasteboard, type.utf8().data(), &string);
    if (!string.length)
        return String();

    String returnValue({ string.data, string.length });

    wpe_pasteboard_string_free(&string);
    return returnValue;
||||||| parent of b0fa7c52d79a (chore(webkit): bootstrap build #1994)
    struct wpe_pasteboard_string string = { nullptr, 0 };
    wpe_pasteboard_get_string(m_pasteboard, type.utf8().data(), &string);
    if (!string.length)
        return String();

    String returnValue(string.data, string.length);

    wpe_pasteboard_string_free(&string);
    return returnValue;
=======
    return sharedPasteboard().get(type);
>>>>>>> b0fa7c52d79a (chore(webkit): bootstrap build #1994)
}

void PlatformPasteboard::write(const PasteboardWebContent& content)
{
    String plainText = "text/plain;charset=utf-8"_s;
    String htmlText = "text/html;charset=utf-8"_s;
    sharedPasteboard().set(plainText, content.text);
    sharedPasteboard().set(htmlText, content.markup);
}

void PlatformPasteboard::write(const String& type, const String& string)
{
    sharedPasteboard().set(type, string);
}

Vector<String> PlatformPasteboard::typesSafeForDOMToReadAndWrite(const String&) const
{
    return { };
}

int64_t PlatformPasteboard::write(const PasteboardCustomData&)
{
    return 0;
}

int64_t PlatformPasteboard::write(const Vector<PasteboardCustomData>&)
{
    return 0;
}

} // namespace WebCore

#endif // USE(LIBWPE)
