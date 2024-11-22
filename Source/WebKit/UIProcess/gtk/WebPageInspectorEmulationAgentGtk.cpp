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
#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "WebPageInspectorEmulationAgent.h"
#include "WebPageProxy.h"
#include <WebCore/IntSize.h>
#include <gtk/gtk.h>

namespace WebKit {

#if USE(GTK4)
bool windowHasManyTabs(GtkWidget* widget) {
    for (GtkWidget* parent = gtk_widget_get_parent(widget); parent; parent = gtk_widget_get_parent(parent)) {
        if (GTK_IS_NOTEBOOK(parent)) {
            int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(parent));
            return pages > 1;
        }
    }
    return false;
}
#endif

void WebPageInspectorEmulationAgent::platformSetSize(int width, int height, Function<void (const String& error)>&& callback)
{
    WebCore::IntSize viewSize(width, height);
    GtkWidget* viewWidget = m_page.viewWidget();
    GtkWidget* window = gtk_widget_get_toplevel(viewWidget);
    if (!window) {
        callback("Cannot find parent window"_s);
        return;
    }
    if (!GTK_IS_WINDOW(window)) {
        callback("Toplevel is not a window"_s);
        return;
    }
    GtkAllocation viewAllocation;
    gtk_widget_get_allocation(viewWidget, &viewAllocation);
#if USE(GTK4)
    // In GTK4 newly added tabs will have allocation size of 0x0, before the tab is shown.
    // This is a Ctrl+click scenario. We invoke callback righ await to not stall.
    if (!viewAllocation.width && !viewAllocation.height && windowHasManyTabs(viewWidget)) {
        callback(String());
        return;
    }
#endif
    if (viewAllocation.width == width && viewAllocation.height == height) {
        callback(String());
        return;
    }

    GtkAllocation windowAllocation;
    gtk_widget_get_allocation(window, &windowAllocation);

    width += windowAllocation.width - viewAllocation.width;
    height += windowAllocation.height - viewAllocation.height;

    if (auto* drawingArea = static_cast<DrawingAreaProxyCoordinatedGraphics*>(m_page.drawingArea())) {
        bool didNotHaveInitialAllocation = !windowAllocation.width && !windowAllocation.height;
        // The callback can only be called if the page is still alive, so we can safely capture `this`.
        drawingArea->waitForSizeUpdate([this, callback = WTFMove(callback), didNotHaveInitialAllocation, viewSize](const DrawingAreaProxyCoordinatedGraphics& drawingArea) mutable {
#if USE(GTK4)
            if (viewSize == drawingArea.size()) {
                callback(String());
                return;
            }
            if (didNotHaveInitialAllocation) {
                // In gtk4 resize request may be lost (overridden by default one) if the window is not yet
                // allocated when we are changing the size, so we try again.
                platformSetSize(viewSize.width(), viewSize.height(), WTFMove(callback));
                return;
            }
            callback("Failed to resize window"_s);
#else
            UNUSED_PARAM(this);
            UNUSED_PARAM(didNotHaveInitialAllocation);
            UNUSED_PARAM(drawingArea);
            callback(String());
#endif
        });
    } else {
        callback("No backing store for window"_s);
    }
#if USE(GTK4)
    // Depending on whether default size has been applied or not, we need to
    // do one of the calls, so we just do both.
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_widget_set_size_request(window, width, height);
#else
    gtk_window_resize(GTK_WINDOW(window), width, height);
#endif
}

} // namespace WebKit
