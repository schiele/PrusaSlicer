///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#import "MacDarkMode.hpp"

#include <wx/window.h>
#include "wx/osx/core/cfstring.h"

#import <algorithm>

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <AppKit/NSScreen.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

@interface MacDarkMode : NSObject {}
@end


@implementation MacDarkMode

namespace Slic3r {
namespace GUI {

bool mac_dark_mode()
{
    NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style isEqualToString:@"Dark"];

}

double mac_max_scaling_factor()
{
    double scaling = 1.;
    if ([NSScreen screens] == nil) {
        scaling = [[NSScreen mainScreen] backingScaleFactor];
    } else {
	    for (int i = 0; i < [[NSScreen screens] count]; ++ i)
	    	scaling = std::max<double>(scaling, [[[NSScreen screens] objectAtIndex:0] backingScaleFactor]);
	}
    return scaling;
}

void WKWebView_evaluateJavaScript(void * web, wxString const & script, void (*callback)(wxString const &))
{
    [(WKWebView*)web evaluateJavaScript:wxCFStringRef(script).AsNSString() completionHandler: ^(id result, NSError *error) {
        if (callback && error != nil) {
            wxString err = wxCFStringRef(error.localizedFailureReason).AsString();
            callback(err);
        }
    }];
}

// Recursively search for NSOutlineView in the view hierarchy
static NSOutlineView* findOutlineView(NSView *view)
{
    if ([view isKindOfClass:[NSOutlineView class]])
        return (NSOutlineView *)view;
    for (NSView *subview in [view subviews]) {
        NSOutlineView *found = findOutlineView(subview);
        if (found) return found;
    }
    return nil;
}

void mac_disable_horizontal_scroll(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    NSView *parent = view;
    while (parent && ![parent isKindOfClass:[NSScrollView class]])
        parent = [parent superview];
    if ([parent isKindOfClass:[NSScrollView class]])
        [(NSScrollView *)parent setHasHorizontalScroller:NO];
}

int mac_get_outlineview_name_width(void *nsview_handle)
{
    if (!nsview_handle)
        return -1;
    NSView *view = (__bridge NSView *)nsview_handle;

    // Find enclosing NSScrollView
    NSView *parent = view;
    while (parent && ![parent isKindOfClass:[NSScrollView class]])
        parent = [parent superview];
    NSScrollView *scrollView = nil;
    if ([parent isKindOfClass:[NSScrollView class]])
        scrollView = (NSScrollView *)parent;

    // Find the NSOutlineView
    NSOutlineView *outlineView = findOutlineView(view);
    if (!outlineView || outlineView.numberOfColumns < 2)
        return -1;

    // Visible width from the clip view (actual drawable area)
    CGFloat visibleWidth = scrollView
        ? [scrollView.contentView bounds].size.width
        : [outlineView bounds].size.width;

    // Sum widths of all columns except column 0
    CGFloat otherColumnsWidth = 0;
    for (NSInteger i = 1; i < outlineView.numberOfColumns; i++)
        otherColumnsWidth += [[outlineView.tableColumns objectAtIndex:i] width];

    // Empirically measure NSOutlineView's overhead (intercell spacing, outline
    // indentation, internal padding) by temporarily forcing column 0 wide,
    // reading the resulting frame, and computing everything-except-column-0.
    NSTableColumn *col0 = [outlineView.tableColumns objectAtIndex:0];
    CGFloat savedWidth = col0.width;

    // Force column 0 wide enough to guarantee overflow
    [col0 setWidth:visibleWidth];
    [outlineView tile]; // synchronous relayout

    // "X" = total width consumed by everything except column 0
    CGFloat X = outlineView.frame.size.width - visibleWidth;
    CGFloat nameWidth = visibleWidth - X;

    // Restore if calculation failed, otherwise leave at new width
    if (nameWidth <= 0) {
        [col0 setWidth:savedWidth];
        return -1;
    }

    // Set column 0 to exact fit and re-tile
    [col0 setWidth:nameWidth];
    // Let the last column absorb any remaining sub-pixel space so the native
    // trailing column separator is pushed to the very edge and not visible.
    [outlineView setColumnAutoresizingStyle:NSTableViewLastColumnOnlyAutoresizingStyle];
    [outlineView tile];

    return (int)nameWidth;
}

void mac_set_staticbox_transparent(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    // wxStaticBox wraps an NSBox — find it in the hierarchy
    NSView *current = view;
    while (current && ![current isKindOfClass:[NSBox class]])
        current = [current superview];
    if ([current isKindOfClass:[NSBox class]]) {
        NSBox *box = (NSBox *)current;
        // preFlight: Make the NSBox fully transparent with no native title.
        // OnPaintMac() in FlatStaticBox handles ALL visual rendering (border,
        // background, label text).  Setting NSNoTitle is critical: it prevents
        // the native title cell from influencing wxStaticBoxSizer's content
        // margins via GetBordersForSizer(), which otherwise causes a mismatch
        // between the native layout and our custom-drawn border position.
        [box setBoxType:NSBoxCustom];
        [box setTransparent:YES];
        [box setBorderType:NSNoBorder];
        [box setBorderWidth:0];
        [box setTitlePosition:NSNoTitle];
    }
}

void mac_set_staticbox_colors(void *nsview_handle,
                              unsigned char fill_r, unsigned char fill_g, unsigned char fill_b,
                              unsigned char border_r, unsigned char border_g, unsigned char border_b,
                              unsigned char title_r, unsigned char title_g, unsigned char title_b)
{
    if (!nsview_handle)
        return;
    @try {
        NSView *view = (__bridge NSView *)nsview_handle;
        if (!view)
            return;
        // Find the NSBox — GetHandle() may return it directly or a child view
        NSView *current = view;
        while (current && ![current isKindOfClass:[NSBox class]])
            current = [current superview];
        if (!current || ![current isKindOfClass:[NSBox class]])
            return;
        NSBox *box = (NSBox *)current;
        [box setFillColor:[NSColor colorWithSRGBRed:fill_r/255.0 green:fill_g/255.0
                                               blue:fill_b/255.0 alpha:1.0]];
        [box setBorderColor:[NSColor colorWithSRGBRed:border_r/255.0 green:border_g/255.0
                                                 blue:border_b/255.0 alpha:1.0]];
        // Style the title: wxNSBox doesn't support setAttributedTitle:, so
        // set the title cell's text color directly via the NSBox's title cell.
        NSCell *titleCell = [box titleCell];
        if (titleCell && [titleCell respondsToSelector:@selector(setTextColor:)]) {
            [(NSTextFieldCell *)titleCell setTextColor:
                [NSColor colorWithSRGBRed:title_r/255.0 green:title_g/255.0
                                     blue:title_b/255.0 alpha:1.0]];
        }
    } @catch (NSException *e) {
        NSLog(@"mac_set_staticbox_colors exception: %@", e);
    }
}

void mac_set_textfield_background(void *nsview_handle,
                                  unsigned char r, unsigned char g, unsigned char b)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    // wxTextCtrl wraps NSTextField (single-line) or NSScrollView+NSTextView (multi-line).
    // Disable native background drawing entirely — the parent StaticBox::doRender()
    // handles all background painting (including disabled state colors).
    // This prevents the native NSTextField disabled appearance from showing through.
    if ([view isKindOfClass:[NSTextField class]]) {
        NSTextField *field = (NSTextField *)view;
        [field setDrawsBackground:NO];
    } else if ([view isKindOfClass:[NSScrollView class]]) {
        NSScrollView *sv = (NSScrollView *)view;
        [sv setDrawsBackground:NO];
        NSTextView *tv = (NSTextView *)[sv documentView];
        if (tv && [tv isKindOfClass:[NSTextView class]])
            [tv setDrawsBackground:NO];
    }
}

// NOTE: mac_set_treectrl_outline_selection is no longer needed.
// Selection styling is now handled by preFlightRendererNative in GUI_App.cpp,
// which overrides DrawItemSelectionRect to draw a 1px outline instead of a fill.
void mac_set_treectrl_outline_selection(void * /*nsview_handle*/)
{
    // No-op — kept for API compatibility.
}

void mac_set_window_transparent(void *nsview_handle)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    NSWindow *window = [view window];
    if (window) {
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
    }
}

void mac_set_view_corner_radius(void *nsview_handle, double radius)
{
    if (!nsview_handle)
        return;
    NSView *view = (__bridge NSView *)nsview_handle;
    [view setWantsLayer:YES];
    view.layer.cornerRadius = radius;
    view.layer.masksToBounds = YES;
}

}
}

@end
