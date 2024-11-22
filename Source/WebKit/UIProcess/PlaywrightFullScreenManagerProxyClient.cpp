/*
 * Copyright (C) 2023 Microsoft Corporation.
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
#include "PlaywrightFullScreenManagerProxyClient.h"

#if ENABLE(FULLSCREEN_API)

#include "WebPageProxy.h"

namespace WebKit {
using namespace WebCore;

PlaywrightFullScreenManagerProxyClient::PlaywrightFullScreenManagerProxyClient(WebPageProxy& page)
    : m_pageProxy(page)
{
}

void PlaywrightFullScreenManagerProxyClient::enterFullScreen()
{
    m_pageProxy.fullScreenManager()->willEnterFullScreen();
    m_pageProxy.fullScreenManager()->didEnterFullScreen();
}

void PlaywrightFullScreenManagerProxyClient::exitFullScreen()
{
    m_pageProxy.fullScreenManager()->willExitFullScreen();
    m_pageProxy.fullScreenManager()->didExitFullScreen();

}

} // namespace WebKit

#endif // ENABLE(FULLSCREEN_API)
