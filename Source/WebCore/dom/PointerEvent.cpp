/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "PointerEvent.h"

#include "EventNames.h"
#include "MouseEvent.h"
#include "Node.h"
#include "PlatformMouseEvent.h"
#include "PointerEventTypeNames.h"
#include "PlatformTouchEvent.h"
#include <wtf/IsoMallocInlines.h>

namespace WebCore {

WTF_MAKE_ISO_ALLOCATED_IMPL(PointerEvent);

static AtomString pointerEventType(const AtomString& mouseEventType)
{
    auto& names = eventNames();
    if (mouseEventType == names.mousedownEvent)
        return names.pointerdownEvent;
    if (mouseEventType == names.mouseoverEvent)
        return names.pointeroverEvent;
    if (mouseEventType == names.mouseenterEvent)
        return names.pointerenterEvent;
    if (mouseEventType == names.mousemoveEvent)
        return names.pointermoveEvent;
    if (mouseEventType == names.mouseleaveEvent)
        return names.pointerleaveEvent;
    if (mouseEventType == names.mouseoutEvent)
        return names.pointeroutEvent;
    if (mouseEventType == names.mouseupEvent)
        return names.pointerupEvent;

    return nullAtom();
}

RefPtr<PointerEvent> PointerEvent::create(MouseButton button, const MouseEvent& mouseEvent, PointerID pointerId, const String& pointerType)
{
    auto type = pointerEventType(mouseEvent.type());
    if (type.isEmpty())
        return nullptr;

    return create(type, button, mouseEvent, pointerId, pointerType);
}

Ref<PointerEvent> PointerEvent::create(const AtomString& type, MouseButton button, const MouseEvent& mouseEvent, PointerID pointerId, const String& pointerType)
{
    return adoptRef(*new PointerEvent(type, button, mouseEvent, pointerId, pointerType));
}

Ref<PointerEvent> PointerEvent::create(const AtomString& type, PointerID pointerId, const String& pointerType, IsPrimary isPrimary)
{
    return adoptRef(*new PointerEvent(type, pointerId, pointerType, isPrimary));
}

PointerEvent::PointerEvent()
    : MouseEvent(EventInterfaceType::PointerEvent)
{
}

PointerEvent::PointerEvent(const AtomString& type, Init&& initializer)
    : MouseEvent(EventInterfaceType::PointerEvent, type, initializer)
    , m_pointerId(initializer.pointerId)
    , m_width(initializer.width)
    , m_height(initializer.height)
    , m_pressure(initializer.pressure)
    , m_tangentialPressure(initializer.tangentialPressure)
    , m_tiltX(initializer.tiltX)
    , m_tiltY(initializer.tiltY)
    , m_twist(initializer.twist)
    , m_pointerType(initializer.pointerType)
    , m_isPrimary(initializer.isPrimary)
{
}

PointerEvent::PointerEvent(const AtomString& type, MouseButton button, const MouseEvent& mouseEvent, PointerID pointerId, const String& pointerType)
    : MouseEvent(EventInterfaceType::PointerEvent, type, typeCanBubble(type), typeIsCancelable(type), typeIsComposed(type), mouseEvent.view(), mouseEvent.detail(), mouseEvent.screenLocation(),
        { mouseEvent.clientX(), mouseEvent.clientY() }, mouseEvent.movementX(), mouseEvent.movementY(), mouseEvent.modifierKeys(), button, mouseEvent.buttons(),
        mouseEvent.syntheticClickType(), mouseEvent.relatedTarget())
    , m_pointerId(pointerId)
    // MouseEvent is a misnomer in this context, and can represent events from a pressure sensitive input device if the pointer type is "Pen" or "Touch".
    // If it does represent a pressure sensitive input device, we consult MouseEvent::force() for the event pressure, else we fall back to spec defaults.
    , m_pressure(pointerType != mousePointerEventType() ? std::clamp(mouseEvent.force(), 0., 1.) : pressureForPressureInsensitiveInputDevices(buttons()))
    , m_pointerType(pointerType)
    , m_isPrimary(true)
{
}

PointerEvent::PointerEvent(const AtomString& type, PointerID pointerId, const String& pointerType, IsPrimary isPrimary)
    : MouseEvent(EventInterfaceType::PointerEvent, type, typeCanBubble(type), typeIsCancelable(type), typeIsComposed(type), nullptr, 0, { }, { }, 0, 0, { }, buttonForType(type), buttonsForType(type), SyntheticClickType::NoTap, nullptr)
    , m_pointerId(pointerId)
    // FIXME: This may be wrong because we can create an event from a pressure sensitive device.
    // We don't have a backing MouseEvent to consult pressure/force information from, though, so let's do the next best thing.
    , m_pressure(pressureForPressureInsensitiveInputDevices(buttons()))
    , m_pointerType(pointerType)
    , m_isPrimary(isPrimary == IsPrimary::Yes)
{
}

PointerEvent::~PointerEvent() = default;

#if ENABLE(TOUCH_EVENTS) && !PLATFORM(IOS_FAMILY) && !PLATFORM(WPE)

static const AtomString& pointerEventType(PlatformTouchPoint::State state)
{
    switch (state) {
    case PlatformTouchPoint::State::TouchPressed:
        return eventNames().pointerdownEvent;
    case PlatformTouchPoint::State::TouchMoved:
        return eventNames().pointermoveEvent;
    case PlatformTouchPoint::State::TouchStationary:
        return eventNames().pointermoveEvent;
    case PlatformTouchPoint::State::TouchReleased:
        return eventNames().pointerupEvent;
    case PlatformTouchPoint::State::TouchCancelled:
        return eventNames().pointercancelEvent;
    case PlatformTouchPoint::State::TouchStateEnd:
        break;
    }
    ASSERT_NOT_REACHED();
    return nullAtom();
}

Ref<PointerEvent> PointerEvent::create(const PlatformTouchEvent& event, unsigned index, bool isPrimary, Ref<WindowProxy>&& view, const IntPoint& touchDelta)
{
    const auto& type = pointerEventType(event.touchPoints().at(index).state());
    return adoptRef(*new PointerEvent(type, event, typeIsCancelable(type), index, isPrimary, WTFMove(view), touchDelta));
}

Ref<PointerEvent> PointerEvent::create(const AtomString& type, const PlatformTouchEvent& event, unsigned index, bool isPrimary, Ref<WindowProxy>&& view, const IntPoint& touchDelta)
{
    return adoptRef(*new PointerEvent(type, event, typeIsCancelable(type), index, isPrimary, WTFMove(view), touchDelta));
}

PointerEvent::PointerEvent(const AtomString& type, const PlatformTouchEvent& event, IsCancelable isCancelable, unsigned index, bool isPrimary, Ref<WindowProxy>&& view, const IntPoint& touchDelta)
    : MouseEvent(EventInterfaceType::PointerEvent, type, typeCanBubble(type), isCancelable, typeIsComposed(type), event.timestamp().approximateMonotonicTime(), WTFMove(view), 0,
        event.touchPoints().at(index).pos(), event.touchPoints().at(index).pos(), touchDelta.x(), touchDelta.y(), event.modifiers(), buttonForType(type), buttonsForType(type), nullptr, 0, SyntheticClickType::NoTap, IsSimulated::No, IsTrusted::Yes)
    , m_pointerId(event.touchPoints().at(index).id())
    , m_width(2 * event.touchPoints().at(index).radiusX())
    , m_height(2 * event.touchPoints().at(index).radiusY())
    , m_pressure(event.touchPoints().at(index).force())
    , m_pointerType(touchPointerEventType())
    , m_isPrimary(isPrimary)
{
}

#endif // ENABLE(TOUCH_EVENTS) && !PLATFORM(IOS_FAMILY)

} // namespace WebCore
