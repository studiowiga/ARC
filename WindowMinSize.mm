#include "WindowMinSize.h"

#import <Cocoa/Cocoa.h>
#include <algorithm>

//==============================================================================
//  macOS : taille de contenu minimale sur la fenêtre du DAW
//==============================================================================
namespace wiga
{
    struct WindowMinSize::Impl
    {
        NSView*   view = nil;          // vue JUCE du plugin
        NSWindow* window = nil;        // fenêtre du DAW
        NSSize    previousMin { 0, 0 };
        std::function<int()> logicalWidth;
        int minW = 0, minH = 0;

        bool computeMinContent (NSSize& out) const
        {
            if (view == nil || window == nil)
                return false;

            // position de la pédale dans la fenêtre du DAW (repère de la fenêtre, origine en bas à gauche)
            const NSRect inWin   = [view convertRect: [view bounds] toView: nil];
            const NSSize content = [window contentRectForFrameRect: [window frame]].size;

            const CGFloat left = std::max ((CGFloat) 0, inWin.origin.x);
            const CGFloat top  = std::max ((CGFloat) 0, content.height - (inWin.origin.y + inWin.size.height));

            out = NSMakeSize (left + (CGFloat) minW, top + (CGFloat) minH);
            return true;
        }

        void apply()
        {
            NSSize m;
            if (window != nil && computeMinContent (m))
                [window setContentMinSize: NSMakeSize (std::max (m.width,  previousMin.width),
                                                       std::max (m.height, previousMin.height))];
        }
    };

    WindowMinSize::WindowMinSize() : impl (new Impl()) {}
    WindowMinSize::~WindowMinSize() { detach(); delete impl; }

    bool WindowMinSize::isAttachedTo (void* nativeHandle) const
    {
        NSView* v = (NSView*) nativeHandle;
        return v != nil && impl->view == v && impl->window == [v window];
    }

    void WindowMinSize::attach (void* nativeHandle, std::function<int()> currentLogicalWidth)
    {
        detach();
        NSView* v = (NSView*) nativeHandle;
        if (v == nil)
            return;

        impl->view = v;
        impl->logicalWidth = std::move (currentLogicalWidth);
        impl->window = [v window];

        if (impl->window != nil)
        {
            impl->previousMin = [impl->window contentMinSize];
            impl->apply();
        }
    }

    void WindowMinSize::detach()
    {
        if (impl->window != nil)
            [impl->window setContentMinSize: impl->previousMin];
        impl->window = nil;
        impl->view = nil;
    }

    void WindowMinSize::setMinimumSize (int logicalW, int logicalH)
    {
        impl->minW = logicalW;
        impl->minH = logicalH;
        impl->apply();
    }

    void WindowMinSize::enforceNow()
    {
        NSSize m;
        if (impl->window == nil || ! impl->computeMinContent (m))
            return;
        if ([impl->window styleMask] & NSWindowStyleMaskFullScreen)
            return;

        impl->apply();
        const NSRect content = [impl->window contentRectForFrameRect: [impl->window frame]];
        if (content.size.width < m.width - 0.5 || content.size.height < m.height - 0.5)
        {
            NSRect r = content;
            const CGFloat newW = std::max (content.size.width, m.width);
            const CGFloat newH = std::max (content.size.height, m.height);
            r.origin.y -= (newH - content.size.height);   // garde le haut de la fenêtre en place
            r.size = NSMakeSize (newW, newH);
            [impl->window setFrame: [impl->window frameRectForContentRect: r] display: YES];
        }
    }
}
