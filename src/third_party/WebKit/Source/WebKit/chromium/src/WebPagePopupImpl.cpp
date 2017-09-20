/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
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
#include "WebPagePopupImpl.h"

#include "PageWidgetDelegate.h"
#include "WebCursorInfo.h"
#include "WebInputEventConversion.h"
#include "WebPagePopup.h"
#include "WebSettingsImpl.h"
#include "WebViewClient.h"
#include "WebViewImpl.h"
#include "WebWidgetClient.h"
#include "core/dom/ContextFeatures.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/EmptyClients.h"
#include "core/page/Chrome.h"
#include "core/page/DOMWindowPagePopup.h"
#include "core/page/EventHandler.h"
#include "core/page/FocusController.h"
#include "core/page/FrameView.h"
#include "core/page/Page.h"
#include "core/page/PagePopupClient.h"
#include "core/page/Settings.h"

using namespace WebCore;
using namespace std;

namespace WebKit {

#if ENABLE(PAGE_POPUP)

class PagePopupChromeClient : public EmptyChromeClient, public WebCore::PageClientChromium {
    WTF_MAKE_NONCOPYABLE(PagePopupChromeClient);
    WTF_MAKE_FAST_ALLOCATED;

public:
    explicit PagePopupChromeClient(WebPagePopupImpl* popup)
        : m_popup(popup)
    {
        ASSERT(m_popup->widgetClient());
    }

private:
    virtual void closeWindowSoon() OVERRIDE
    {
        m_popup->closePopup();
    }

    virtual FloatRect windowRect() OVERRIDE
    {
        return FloatRect(m_popup->m_windowRectInScreen.x, m_popup->m_windowRectInScreen.y, m_popup->m_windowRectInScreen.width, m_popup->m_windowRectInScreen.height);
    }

    virtual void setWindowRect(const FloatRect& rect) OVERRIDE
    {
        m_popup->m_windowRectInScreen = IntRect(rect);
        m_popup->widgetClient()->setWindowRect(m_popup->m_windowRectInScreen);
    }

    virtual void addMessageToConsole(MessageSource, MessageLevel, const String& message, unsigned lineNumber, const String&) OVERRIDE
    {
#ifndef NDEBUG
        fprintf(stderr, "CONSOLE MESSSAGE:%u: %s\n", lineNumber, message.utf8().data());
#else
        UNUSED_PARAM(message);
        UNUSED_PARAM(lineNumber);
#endif
    }

    virtual void invalidateContentsAndRootView(const IntRect& paintRect) OVERRIDE
    {
        if (paintRect.isEmpty())
            return;
        m_popup->widgetClient()->didInvalidateRect(paintRect);
    }

    virtual void scroll(const IntSize& scrollDelta, const IntRect& scrollRect, const IntRect& clipRect) OVERRIDE
    {
        m_popup->widgetClient()->didScrollRect(scrollDelta.width(), scrollDelta.height(), intersection(scrollRect, clipRect));
    }

    virtual void invalidateContentsForSlowScroll(const IntRect& updateRect) OVERRIDE
    {
        invalidateContentsAndRootView(updateRect);
    }

    virtual void scheduleAnimation() OVERRIDE
    {
        m_popup->widgetClient()->scheduleAnimation();
    }

    virtual void* webView() const OVERRIDE
    {
        return m_popup->m_webView;
    }

    virtual FloatSize minimumWindowSize() const OVERRIDE
    {
        return FloatSize(0, 0);
    }

    virtual PlatformPageClient platformPageClient() const OVERRIDE
    {
        return PlatformPageClient(this);
    }

    virtual void setCursor(const WebCore::Cursor& cursor) OVERRIDE
    {
        if (m_popup->m_webView->client())
            m_popup->m_webView->client()->didChangeCursor(WebCursorInfo(cursor));
    }

    virtual void needTouchEvents(bool needsTouchEvents) OVERRIDE
    {
        m_popup->widgetClient()->hasTouchEventHandlers(needsTouchEvents);
    }

    // PageClientChromium methods:
    virtual WebKit::WebScreenInfo screenInfo() OVERRIDE
    {
        return m_popup->m_webView->client()->screenInfo();
    }

    WebPagePopupImpl* m_popup;
};

class PagePopupFeaturesClient : public ContextFeaturesClient {
    virtual bool isEnabled(Document*, ContextFeatures::FeatureType, bool) OVERRIDE;
};

bool PagePopupFeaturesClient::isEnabled(Document*, ContextFeatures::FeatureType type, bool defaultValue)
{
    if (type == ContextFeatures::PagePopup)
        return true;
    return defaultValue;
}

// WebPagePopupImpl ----------------------------------------------------------------

WebPagePopupImpl::WebPagePopupImpl(WebWidgetClient* client)
    : m_widgetClient(client)
    , m_closing(false)
{
    ASSERT(client);
}

WebPagePopupImpl::~WebPagePopupImpl()
{
    ASSERT(!m_page);
}

bool WebPagePopupImpl::initialize(WebViewImpl* webView, PagePopupClient* popupClient, const IntRect&)
{
    ASSERT(webView);
    ASSERT(popupClient);
    m_webView = webView;
    m_popupClient = popupClient;

    resize(m_popupClient->contentSize());

    if (!initializePage())
        return false;
    m_widgetClient->show(WebNavigationPolicy());
    setFocus(true);

    return true;
}

bool WebPagePopupImpl::initializePage()
{
    Page::PageClients pageClients;
    fillWithEmptyClients(pageClients);
    m_chromeClient = adoptPtr(new PagePopupChromeClient(this));
    pageClients.chromeClient = m_chromeClient.get();

    m_page = adoptPtr(new Page(pageClients));
    m_page->settings()->setScriptEnabled(true);
    m_page->settings()->setAllowScriptsToCloseWindows(true);
    m_page->setDeviceScaleFactor(m_webView->deviceScaleFactor());
    m_page->settings()->setDeviceSupportsTouch(m_webView->page()->settings()->deviceSupportsTouch());

    unsigned layoutMilestones = DidFirstLayout | DidFirstVisuallyNonEmptyLayout;
    m_page->addLayoutMilestones(static_cast<LayoutMilestones>(layoutMilestones));

    static ContextFeaturesClient* pagePopupFeaturesClient =  new PagePopupFeaturesClient();
    provideContextFeaturesTo(m_page.get(), pagePopupFeaturesClient);
    static FrameLoaderClient* emptyFrameLoaderClient =  new EmptyFrameLoaderClient();
    RefPtr<Frame> frame = Frame::create(m_page.get(), 0, emptyFrameLoaderClient);
    frame->setView(FrameView::create(frame.get()));
    frame->init();
    frame->view()->resize(m_popupClient->contentSize());
    frame->view()->setTransparent(false);

    DOMWindowPagePopup::install(frame->document()->domWindow(), m_popupClient);

    DocumentWriter* writer = frame->loader()->activeDocumentLoader()->writer();
    writer->setMIMEType("text/html");
    writer->setEncoding("UTF-8", false);
    writer->begin();
    m_popupClient->writeDocument(*writer);
    writer->end();
    return true;
}

void WebPagePopupImpl::destroyPage()
{
    if (!m_page)
        return;

    if (m_page->mainFrame())
        m_page->mainFrame()->loader()->frameDetached();

    m_page.clear();
}

WebSize WebPagePopupImpl::size()
{
    return m_popupClient->contentSize();
}

void WebPagePopupImpl::animate(double)
{
    PageWidgetDelegate::animate(m_page.get(), monotonicallyIncreasingTime());
}

void WebPagePopupImpl::layout()
{
    PageWidgetDelegate::layout(m_page.get());
}

void WebPagePopupImpl::paint(WebCanvas* canvas, const WebRect& rect, PaintOptions)
{
    if (!m_closing)
        PageWidgetDelegate::paint(m_page.get(), 0, canvas, rect, PageWidgetDelegate::Opaque);
}

void WebPagePopupImpl::resize(const WebSize& newSize)
{
    m_windowRectInScreen = WebRect(m_windowRectInScreen.x, m_windowRectInScreen.y, newSize.width, newSize.height);
    m_widgetClient->setWindowRect(m_windowRectInScreen);

    if (m_page)
        m_page->mainFrame()->view()->resize(newSize);
    m_widgetClient->didInvalidateRect(WebRect(0, 0, newSize.width, newSize.height));
}

bool WebPagePopupImpl::handleKeyEvent(const WebKeyboardEvent&)
{
    // The main WebView receives key events and forward them to this via handleKeyEvent().
    ASSERT_NOT_REACHED();
    return false;
}

bool WebPagePopupImpl::handleCharEvent(const WebKeyboardEvent&)
{
    // The main WebView receives key events and forward them to this via handleKeyEvent().
    ASSERT_NOT_REACHED();
    return false;
}

bool WebPagePopupImpl::handleGestureEvent(const WebGestureEvent& event)
{
    if (m_closing || !m_page || !m_page->mainFrame() || !m_page->mainFrame()->view())
        return false;
    Frame& frame = *m_page->mainFrame();
    return frame.eventHandler()->handleGestureEvent(PlatformGestureEventBuilder(frame.view(), event));
}

bool WebPagePopupImpl::handleInputEvent(const WebInputEvent& event)
{
    if (m_closing)
        return false;
    return PageWidgetDelegate::handleInputEvent(m_page.get(), *this, event);
}

bool WebPagePopupImpl::handleKeyEvent(const PlatformKeyboardEvent& event)
{
    if (m_closing || !m_page->mainFrame() || !m_page->mainFrame()->view())
        return false;
    return m_page->mainFrame()->eventHandler()->keyEvent(event);
}

void WebPagePopupImpl::setFocus(bool enable)
{
    if (!m_page)
        return;
    m_page->focusController()->setFocused(enable);
    if (enable)
        m_page->focusController()->setActive(true);
}

void WebPagePopupImpl::close()
{
    m_closing = true;
    destroyPage(); // In case closePopup() was not called.
    m_widgetClient = 0;
    deref();
}

void WebPagePopupImpl::closePopup()
{
    if (m_page) {
        m_page->clearPageGroup();
        m_page->mainFrame()->loader()->stopAllLoaders();
        m_page->mainFrame()->loader()->stopLoading(UnloadEventPolicyNone);
        DOMWindowPagePopup::uninstall(m_page->mainFrame()->document()->domWindow());
    }
    m_closing = true;

    destroyPage();

    // m_widgetClient might be 0 because this widget might be already closed.
    if (m_widgetClient) {
        // closeWidgetSoon() will call this->close() later.
        m_widgetClient->closeWidgetSoon();
    }

    m_popupClient->didClosePopup();
}

#endif // ENABLE(PAGE_POPUP)

// WebPagePopup ----------------------------------------------------------------

WebPagePopup* WebPagePopup::create(WebWidgetClient* client)
{
#if ENABLE(PAGE_POPUP)
    if (!client)
        CRASH();
    // A WebPagePopupImpl instance usually has two references.
    //  - One owned by the instance itself. It represents the visible widget.
    //  - One owned by a WebViewImpl. It's released when the WebViewImpl ask the
    //    WebPagePopupImpl to close.
    // We need them because the closing operation is asynchronous and the widget
    // can be closed while the WebViewImpl is unaware of it.
    return adoptRef(new WebPagePopupImpl(client)).leakRef();
#else
    UNUSED_PARAM(client);
    return 0;
#endif
}

} // namespace WebKit
