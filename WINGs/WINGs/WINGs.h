/* WINGs.h
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _WINGS_H_
#define _WINGS_H_

#include <wraster.h>
#include <WINGs/WUtil.h>
#include <xcb/xcb.h>
#include <stdbool.h>
#include <stdint.h>

#define WINGS_H_VERSION  20260902


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#if 0
}
#endif


typedef unsigned long WMPixel;


typedef struct {
    unsigned int width;
    unsigned int height;
} WMSize;

typedef struct {
    int x;
    int y;
} WMPoint;

typedef struct {
    WMPoint pos;
    WMSize size;
} WMRect;





#define ClientMessageMask	(1L<<30)


#ifndef _DEFINED_GNUSTEP_WINDOW_INFO
#define	_DEFINED_GNUSTEP_WINDOW_INFO
/*
 * Window levels are taken from GNUstep (gui/AppKit/NSWindow.h)
 * NSDesktopWindowLevel intended to be the level at which things
 * on the desktop sit ... so you should be able
 * to put a desktop background just below it.
 *
 * Applications are actually permitted to use any value in the
 * range INT_MIN+1 to INT_MAX
 */
enum {
    WMDesktopWindowLevel = -1000, /* GNUstep addition     */
    WMNormalWindowLevel = 0,
    WMFloatingWindowLevel = 3,
    WMSubmenuWindowLevel = 3,
    WMTornOffMenuWindowLevel = 3,
    WMMainMenuWindowLevel = 20,
    WMDockWindowLevel = 21,       /* Deprecated - use NSStatusWindowLevel */
    WMStatusWindowLevel = 21,
    WMModalPanelWindowLevel = 100,
    WMPopUpMenuWindowLevel = 101,
    WMScreenSaverWindowLevel = 1000
};


/* window attributes */
enum {
    WMBorderlessWindowMask = 0,
    WMTitledWindowMask = 1,
    WMClosableWindowMask = 2,
    WMMiniaturizableWindowMask = 4,
    WMResizableWindowMask = 8,
    WMIconWindowMask = 64,
    WMMiniWindowMask = 128
};
#endif


/* button types */
typedef enum {
    /* 0 is reserved for internal use */
    WBTMomentaryPush = 1,
    WBTPushOnPushOff = 2,
    WBTToggle = 3,
    WBTSwitch = 4,
    WBTRadio = 5,
    WBTMomentaryChange = 6,
    WBTOnOff = 7,
    WBTMomentaryLight = 8,
    WBTTriState = 9
} WMButtonType;

/* button behaviour masks */
enum {
    WBBSpringLoadedMask = (1 << 0),
    WBBPushInMask       = (1 << 1),
    WBBPushChangeMask   = (1 << 2),
    WBBPushLightMask    = (1 << 3),
    WBBStateLightMask   = (1 << 5),
    WBBStateChangeMask  = (1 << 6),
    WBBStatePushMask    = (1 << 7)
};


/* frame title positions */
typedef enum {
    WTPNoTitle,
    WTPAboveTop,
    WTPAtTop,
    WTPBelowTop,
    WTPAboveBottom,
    WTPAtBottom,
    WTPBelowBottom
} WMTitlePosition;


/* relief types */
typedef enum {
    WRFlat,
    WRSimple,
    WRRaised,
    WRSunken,
    WRGroove,
    WRRidge,
    WRPushed
} WMReliefType;


/* alignment types */
typedef enum {
    WALeft,
    WACenter,
    WARight,
    WAJustified		       /* not valid for textfields */
} WMAlignment;


/* image position */
typedef enum {
    WIPNoImage,
    WIPImageOnly,
    WIPLeft,
    WIPRight,
    WIPBelow,
    WIPAbove,
    WIPOverlaps
} WMImagePosition;


/* scroller arrow position */
typedef enum {
    WSAMaxEnd,
    WSAMinEnd,
    WSANone
} WMScrollArrowPosition;

/* scroller parts */
typedef enum {
    WSNoPart,
    WSDecrementPage,
    WSIncrementPage,
    WSDecrementLine,
    WSIncrementLine,
    WSDecrementWheel,
    WSIncrementWheel,
    WSKnob,
    WSKnobSlot
} WMScrollerPart;

/* usable scroller parts */
typedef enum {
    WSUNoParts,
    WSUOnlyArrows,
    WSUAllParts
} WMUsableScrollerParts;

/* matrix types */
typedef enum {
    WMRadioMode,
    WMHighlightMode,
    WMListMode,
    WMTrackMode
} WMMatrixTypes;


typedef enum {
    WTTopTabsBevelBorder,
    WTNoTabsBevelBorder,
    WTNoTabsLineBorder,
    WTNoTabsNoBorder
} WMTabViewType;


/* text movement types */
enum {
    WMIllegalTextMovement,
    WMReturnTextMovement,
    WMEscapeTextMovement,
    WMTabTextMovement,
    WMBacktabTextMovement,
    WMLeftTextMovement,
    WMRightTextMovement,
    WMUpTextMovement,
    WMDownTextMovement
};

/* text field special events */
enum {
    WMInsertTextEvent,
    WMDeleteTextEvent
};


enum {
    WLNotFound = -1       /* element was not found in WMList */
};


/* drag operations */
typedef enum {
    WDOperationNone = 0,
    WDOperationCopy,
    WDOperationMove,
    WDOperationLink,
    WDOperationAsk,
    WDOperationPrivate
} WMDragOperationType;


typedef enum {
    WMGrayModeColorPanel = 1,
    WMRGBModeColorPanel = 2,
    WMCMYKModeColorPanel = 3,
    WMHSBModeColorPanel = 4,
    WMCustomPaletteModeColorPanel = 5,
    WMColorListModeColorPanel = 6,
    WMWheelModeColorPanel = 7
} WMColorPanelMode;



/* system images */
#define WSIReturnArrow			1
#define WSIHighlightedReturnArrow	2
#define WSIScrollerDimple		3
#define WSIArrowLeft			4
#define WSIHighlightedArrowLeft	        5
#define WSIArrowRight			6
#define WSIHighlightedArrowRight	7
#define WSIArrowUp			8
#define WSIHighlightedArrowUp		9
#define WSIArrowDown			10
#define WSIHighlightedArrowDown		11
#define WSICheckMark			12

enum {
    WLDSSelected = (1 << 16),
    WLDSDisabled = (1 << 17),
    WLDSFocused = (1 << 18),
    WLDSIsBranch = (1 << 19)
};

/* alert panel return values */
enum {
    WAPRDefault = 0,
    WAPRAlternate = 1,
    WAPROther = -1,
    WAPRError = -2
};



/* types of input observers */
enum {
    WIReadMask = (1 << 0),
    WIWriteMask = (1 << 1),
    WIExceptMask = (1 << 2)
};



typedef int W_Class;

enum {
    WC_Window = 0,
    WC_Frame = 1,
    WC_Label = 2,
    WC_Button = 3,
    WC_TextField = 4,
    WC_Scroller	= 5,
    WC_ScrollView = 6,
    WC_List = 7,
    WC_Browser = 8,
    WC_PopUpButton = 9,
    WC_ColorWell = 10,
    WC_Slider = 11,
    WC_Matrix = 12,		       /* not ready */
    WC_SplitView = 13,
    WC_TabView = 14,
    WC_ProgressIndicator = 15,
    WC_MenuView = 16,
    WC_Ruler = 17,
    WC_Text = 18,
	WC_Box = 19,
	WC_MultiSelectPopUpButton = 20
};

/* All widgets must start with the following structure
 * in that order. Used for typecasting to get some generic data */
typedef struct W_WidgetType {
    W_Class widgetClass;
    struct W_View *view;

} W_WidgetType;


#define WMWidgetClass(widget)  	(((W_WidgetType*)(widget))->widgetClass)
#define WMWidgetView(widget)   	(((W_WidgetType*)(widget))->view)


/* widgets */

typedef void WMWidget;

typedef struct W_Pixmap WMPixmap;
typedef struct W_Font	WMFont;
typedef struct W_Color	WMColor;

typedef struct W_Screen WMScreen;

typedef struct W_View WMView;

typedef struct W_Window WMWindow;
typedef struct W_Frame WMFrame;
typedef struct W_Button WMButton;
typedef struct W_Label WMLabel;
typedef struct W_TextField WMTextField;
typedef struct W_Scroller WMScroller;
typedef struct W_ScrollView WMScrollView;
typedef struct W_List WMList;
typedef struct W_Browser WMBrowser;
typedef struct W_PopUpButton WMPopUpButton;
typedef struct W_MultiSelectPopUpButton WMMultiSelectPopUpButton;
typedef struct W_ProgressIndicator WMProgressIndicator;
typedef struct W_ColorWell WMColorWell;
typedef struct W_Slider WMSlider;
typedef struct W_Matrix WMMatrix;      /* not ready */
typedef struct W_SplitView WMSplitView;
typedef struct W_TabView WMTabView;
typedef struct W_Ruler WMRuler;
typedef struct W_Text WMText;
typedef struct W_Box WMBox;


/* not widgets */
typedef struct W_TabViewItem WMTabViewItem;
typedef struct W_MenuItem WMMenuItem;


typedef struct W_FilePanel WMFilePanel;
typedef WMFilePanel WMOpenPanel;
typedef WMFilePanel WMSavePanel;

typedef struct W_FontPanel WMFontPanel;

typedef struct W_ColorPanel WMColorPanel;


/* item for WMList */
typedef struct WMListItem {
    char *text;
    void *clientData;		       /* ptr for user clientdata. */

    unsigned int uflags:16;	       /* flags for the user */
    unsigned int selected:1;
    unsigned int disabled:1;
    unsigned int isBranch:1;
    unsigned int loaded:1;
} WMListItem;

/* struct for message panel */
typedef struct WMAlertPanel {
    WMWindow *win;		       /* window */
    WMBox *vbox;
    WMBox *hbox;
    WMButton *defBtn;		       /* default button */
    WMButton *altBtn;		       /* alternative button */
    WMButton *othBtn;		       /* other button */
    WMLabel *iLbl;		       /* icon label */
    WMLabel *tLbl;		       /* title label */
    WMLabel *mLbl;		       /* message label */
    WMFrame *line;		       /* separator */
    short result;		       /* button that was pushed */
} WMAlertPanel;


typedef struct WMGenericPanel {
    WMWindow *win;
    WMBox *vbox;

    WMLabel *iLbl;
    WMLabel *tLbl;

    WMFrame *line;

    WMFrame *content;

    WMBox *buttonBox;
    WMButton *defBtn;
    WMButton *altBtn;

    short result;
} WMGenericPanel;


typedef struct WMInputPanel {
    WMWindow *win;		       /* window */
    WMButton *defBtn;		       /* default button */
    WMButton *altBtn;		       /* alternative button */
    WMLabel *tLbl;		       /* title label */
    WMLabel *mLbl;		       /* message label */
    WMTextField *text;		       /* text field */
    short result;		       /* button that was pushed */
} WMInputPanel;


/* Basic font styles. Used to easily get one style from another */
typedef enum WMFontStyle {
    WFSNormal = 0,
    WFSBold   = 1,
    WFSItalic = 2,
    WFSBoldItalic = 3
} WMFontStyle;


/* WMRuler: */
typedef struct {
    WMArray  *tabs;             /* a growable array of tabstops */
    unsigned short left;        /* left margin marker */
    unsigned short right;       /* right margin marker */
    unsigned short first;       /* indentation marker for first line only */
    unsigned short body;        /* body indentation marker */
    unsigned short retainCount;
} WMRulerMargins;
/* All indentation and tab markers are _relative_ to the left margin marker */


typedef void WMEventProc(xcb_generic_event_t *event, void *clientData);

typedef void WMEventHook(xcb_generic_event_t *event);

/* self is set to the widget from where the callback is being called and
 * clientData to the data set to with WMSetClientData() */
typedef void WMAction(WMWidget *self, void *clientData);

/* same as WMAction, but for stuff that arent widgets */
typedef void WMAction2(void *self, void *clientData);


/* delegate method like stuff */
typedef void WMListDrawProc(WMList *lPtr, int index, xcb_drawable_t d, char *text,
                            int state, WMRect *rect);


/*
typedef void WMSplitViewResizeSubviewsProc(WMSplitView *sPtr,
                                           unsigned int oldWidth,
                                           unsigned int oldHeight);
*/

typedef void WMSplitViewConstrainProc(WMSplitView *sPtr, int dividerIndex,
                                      int *minSize, int *maxSize);

typedef WMWidget* WMMatrixCreateCellProc(WMMatrix *mPtr);




typedef struct WMBrowserDelegate {
    void *data;

    void (*createRowsForColumn)(struct WMBrowserDelegate *self,
                                WMBrowser *sender, int column, WMList *list);

    char* (*titleOfColumn)(struct WMBrowserDelegate *self, WMBrowser *sender,
                           int column);

    void (*didScroll)(struct WMBrowserDelegate *self, WMBrowser *sender);

    void (*willScroll)(struct WMBrowserDelegate *self, WMBrowser *sender);
} WMBrowserDelegate;


typedef struct WMTextFieldDelegate {
    void *data;

    void (*didBeginEditing)(struct WMTextFieldDelegate *self,
                            WMNotification *notif);

    void (*didChange)(struct WMTextFieldDelegate *self,
                      WMNotification *notif);

    void (*didEndEditing)(struct WMTextFieldDelegate *self,
                          WMNotification *notif);

    bool (*shouldBeginEditing)(struct WMTextFieldDelegate *self,
                               WMTextField *tPtr);

    bool (*shouldEndEditing)(struct WMTextFieldDelegate *self,
                             WMTextField *tPtr);
} WMTextFieldDelegate;


typedef struct WMTextDelegate {
    void *data;

    bool (*didDoubleClickOnPicture)(struct WMTextDelegate *self,
                                    void *description);

} WMTextDelegate;



typedef struct WMTabViewDelegate {
    void *data;

    void (*didChangeNumberOfItems)(struct WMTabViewDelegate *self,
                                   WMTabView *tabView);

    void (*didSelectItem)(struct WMTabViewDelegate *self, WMTabView *tabView,
                          WMTabViewItem *item);

    bool (*shouldSelectItem)(struct WMTabViewDelegate *self, WMTabView *tabView,
                             WMTabViewItem *item);

    void (*willSelectItem)(struct WMTabViewDelegate *self, WMTabView *tabView,
                           WMTabViewItem *item);
} WMTabViewDelegate;




typedef void WMSelectionCallback(WMView *view, xcb_atom_t selection, xcb_atom_t target,
                                 xcb_timestamp_t timestamp, void *cdata, WMData *data,
                                 bool refused);


typedef struct WMSelectionProcs {
    WMData* (*convertSelection)(WMView *view, xcb_atom_t selection, xcb_atom_t target,
                                void *cdata, xcb_atom_t *type);
    void (*selectionLost)(WMView *view, xcb_atom_t selection, void *cdata);
    void (*selectionDone)(WMView *view, xcb_atom_t selection, xcb_atom_t target,
                          void *cdata);
} WMSelectionProcs;


typedef struct W_DraggingInfo WMDraggingInfo;


/* links a label to a dnd operation. */
typedef struct W_DragOperationtItem WMDragOperationItem;


typedef struct W_DragSourceProcs {
    WMArray* (*dropDataTypes)(WMView *self);
    WMDragOperationType (*wantedDropOperation)(WMView *self);
    WMArray* (*askedOperations)(WMView *self);
    bool (*acceptDropOperation)(WMView *self, WMDragOperationType operation);
    void (*beganDrag)(WMView *self, WMPoint *point);
    void (*endedDrag)(WMView *self, WMPoint *point, bool deposited);
    WMData* (*fetchDragData)(WMView *self, char *type);
    /*Bool (*ignoreModifierKeysWhileDragging)(WMView *view);*/
} WMDragSourceProcs;



typedef struct W_DragDestinationProcs {
    void (*prepareForDragOperation)(WMView *self);
    WMArray* (*requiredDataTypes)(WMView *self, WMDragOperationType request,
                                  WMArray *sourceDataTypes);
    WMDragOperationType (*allowedOperation)(WMView *self,
                                            WMDragOperationType request,
                                            WMArray *sourceDataTypes);
    bool (*inspectDropData)(WMView *self, WMArray *dropData);
    void (*performDragOperation)(WMView *self, WMArray *dropData,
                                 WMArray *operations, WMPoint *dropLocation);
    void (*concludeDragOperation)(WMView *self);
} WMDragDestinationProcs;


/* ---[ WINGs/wmisc.c ]--------------------------------------------------- */


WMPoint wmkpoint(int x, int y);

WMSize wmksize(unsigned int width, unsigned int height);

WMRect wmkrect(int x, int y, unsigned int width, unsigned int height);

#ifdef ANSI_C_DOESNT_LIKE_IT_THIS_WAY
#define wmksize(width, height) (WMSize){(width), (height)}
#define wmkpoint(x, y)         (WMPoint){(x), (y)}
#endif

/* ---[ WINGs/wapplication.c ]-------------------------------------------- */


void WMInitializeApplication(const char *applicationName, int *argc, char **argv);

/* You're supposed to call this funtion before exiting so WINGs can terminate properly */
void WMReleaseApplication(void);

void WMSetResourcePath(const char *path);

/* don't free the returned string */
char* WMGetApplicationName(void);

/* Try to locate resource file. ext may be NULL */
char* WMPathForResourceOfType(const char *resource, const char *ext);

/* ---[ WINGs/widgets.c ]------------------------------------------------- */

WMScreen* WMOpenScreen(const char *display);

WMScreen* WMCreateScreenWithRContext(xcb_connection_t *display, int screen,
                                     RContext *context);

WMScreen* WMCreateScreen(xcb_connection_t *display, int screen);

WMScreen* WMCreateSimpleApplicationScreen(xcb_connection_t *display, int screen);

/* Exit-only, destroys the screen's windows and default fonts. */
void WMShutdownScreen(WMScreen *scr);

void WMScreenMainLoop(WMScreen *scr);

void WMBreakModalLoop(WMScreen *scr);

void WMRunModalLoop(WMScreen *scr, WMView *view);

RContext* WMScreenRContext(WMScreen *scr);

xcb_connection_t* WMScreenDisplay(WMScreen *scr);

int WMScreenDepth(WMScreen *scr);

/* Xft.dpi/96 (or a physical-size fallback), read once at screen
 * creation and refreshed via WMScreenReadUIScale(). Multiply logical
 * pixel dimensions by this to get device pixels. */
double WMScreenUIScale(WMScreen *scr);
void WMScreenReadUIScale(WMScreen *scr);

void WMSetFocusToWidget(WMWidget *widget);

/* ---[ WINGs/wappresource.c ]-------------------------------------------- */

void WMSetApplicationIconImage(WMScreen *app, RImage *image);

RImage* WMGetApplicationIconImage(WMScreen *app);

void WMSetApplicationIconPixmap(WMScreen *app, WMPixmap *icon);

WMPixmap* WMGetApplicationIconPixmap(WMScreen *app);

/* If color==NULL it will use the default color for panels: ae/aa/ae */
WMPixmap* WMCreateApplicationIconBlendedPixmap(WMScreen *scr, const RColor *color);

void WMSetApplicationIconWindow(WMScreen *scr, xcb_window_t window);

/* ---[ WINGs/wevent.c ]-------------------------------------------------- */

WMEventHook* WMHookEventHandler(WMEventHook *handler);

int WMHandleEvent(xcb_generic_event_t *event);

bool WMScreenPending(WMScreen *scr);

void WMCreateEventHandler(WMView *view, uint32_t mask,
                          WMEventProc *eventProc, void *clientData);

void WMDeleteEventHandler(WMView *view, uint32_t mask,
                          WMEventProc *eventProc, void *clientData);

int WMIsDoubleClick(xcb_generic_event_t *event);

/*int WMIsTripleClick(xcb_generic_event_t *event);*/

void WMNextEvent(xcb_connection_t *dpy, xcb_generic_event_t **event);

void WMMaskEvent(xcb_connection_t *dpy, uint32_t mask, xcb_generic_event_t **event);

void WMSetViewNextResponder(WMView *view, WMView *responder);

/* ---[ WINGs/wxevq.c ]--------------------------------------------------- */

/* internal pending-event queue ahead of dispatch, fed by
 * xcb_poll_for_event() on the connection the queue operation receives
 * (the connection registers itself on the first drain, one queue per
 * process), so the WXEvqTake* operations are exact stand-ins for the
 * XCheck* family on the WINGs queue and every consumer sees
 * xcb_generic_event_t.
 *
 * Ownership: events handed out by WXEvqNext / WXEvqTakeMask /
 * WXEvqTakeMaskTimed / WXEvqTakeTyped / WXEvqTakeTypedTimed /
 * WMNextEvent / WMMaskEvent are owned by the caller,
 * free() them exactly once when done. WMHandleEvent() only borrows the
 * buffer it is given: it never frees it and never substitutes a
 * different buffer (motion/expose compression collapses matching
 * queued successors inside WMNextEvent()/WMMaskEvent(), so the
 * surviving event is what the fetcher handed out). Every
 * event-fetching loop therefore frees below its dispatch call, exactly
 * once. WXEvqPeek() lends a pointer the queue still
 * owns, valid until the next queue mutation. WXEvqPutBack() takes
 * ownership of what it is given. WMNextEvent()/WMMaskEvent() return
 * *event == NULL when the connection has died (observable only to
 * callers whose dead hook returns instead of exiting). */

typedef void WXEvqErrorHook(xcb_connection_t *dpy, unsigned int error_code,
                            unsigned int major_opcode, unsigned int minor_opcode,
                            unsigned long resource_id, unsigned long serial);

typedef void WXEvqDeadHook(xcb_connection_t *dpy);

void WXEvqSetErrorHook(WXEvqErrorHook *hook);

/* Hook invoked when the connection dies (xcb_connection_has_error()).
 * Xlib's IO-error handler used to die here. */
void WXEvqSetDeadHook(WXEvqDeadHook *hook);

/* Run the registered dead hook outside the queue's drain, for the
 * fetchers where the error surfaces after W_HandleInputEvents marked
 * it inside the blocked wait. A noreturn hook keeps NULL fetch
 * results away from the consumers. With no hook installed this
 * exits. */
void WXEvqConnectionDead(xcb_connection_t *dpy);

bool WXEvqPending(xcb_connection_t *dpy);

bool WXEvqNext(xcb_connection_t *dpy, xcb_generic_event_t **event_ret);

bool WXEvqPeek(xcb_connection_t *dpy, xcb_generic_event_t **event_ret);

void WXEvqPutBack(xcb_generic_event_t *event);

bool WXEvqTakeMask(xcb_connection_t *dpy, uint32_t mask, xcb_window_t window,
                   xcb_generic_event_t **event_ret);

bool WXEvqTakeTyped(xcb_connection_t *dpy, int type, xcb_window_t window,
                    xcb_generic_event_t **event_ret);

/* blocking take variants, waiting up to timeout_ms (negative waits
 * forever) for a matching event and blocking on poll() on the X fd
 * between queue checks. True if an event was taken, false on timeout,
 * poll error or dead connection. Interruptions consume the deadline
 * rather than restarting the full timeout. The wait never dispatches
 * anything else, a caller that needs WINGs timers or IPC serviced
 * mid-wait must not use these. */
bool WXEvqTakeMaskTimed(xcb_connection_t *dpy, uint32_t mask, xcb_window_t window, xcb_generic_event_t **event_ret, int timeout_ms);

/* match, when not NULL, is applied to every typed event taken from
 * the queue, non-matching events are freed and the wait continues
 * until a matching one arrives or the deadline expires, which lets a
 * caller drop stale late replies while waiting. A NULL match takes
 * the first typed event. */
typedef bool WXEvqMatchProc(const xcb_generic_event_t *event, void *cdata);

bool WXEvqTakeTypedTimed(xcb_connection_t *dpy, int type, xcb_window_t window, WXEvqMatchProc *match, void *cdata, xcb_generic_event_t **event_ret, int timeout_ms);

int WXEvqDropRange(xcb_connection_t *dpy, unsigned long begin, unsigned long end,
                   uint32_t mask);

void WXEvqClear(void);


void WMRelayToNextResponder(WMView *view, xcb_generic_event_t *event);


/* ---[ WINGs/selection.c ]----------------------------------------------- */


bool WMCreateSelectionHandler(WMView *view, xcb_atom_t selection, xcb_timestamp_t timestamp,
                              WMSelectionProcs *procs, void *cdata);

void WMDeleteSelectionHandler(WMView *view, xcb_atom_t selection, xcb_timestamp_t timestamp);

bool WMRequestSelection(WMView *view, xcb_atom_t selection, xcb_atom_t target,
                        xcb_timestamp_t timestamp, WMSelectionCallback *callback,
                        void *cdata);

void WMCancelSelectionRequests(WMView *view);


extern const char *WMSelectionOwnerDidChangeNotification;

/* ---[ WINGs/dragcommon.c ]---------------------------------------------- */

WMArray* WMCreateDragOperationArray(int initialSize);

WMDragOperationItem* WMCreateDragOperationItem(WMDragOperationType type,
                                               char* text);

WMDragOperationType WMGetDragOperationItemType(WMDragOperationItem* item);

char* WMGetDragOperationItemText(WMDragOperationItem* item);

/* ---[ WINGs/dragsource.c ]---------------------------------------------- */

void WMSetViewDragImage(WMView* view, WMPixmap *dragImage);

void WMReleaseViewDragImage(WMView* view);

void WMSetViewDragSourceProcs(WMView *view, WMDragSourceProcs *procs);

bool WMIsDraggingFromView(WMView *view);

void WMDragImageFromView(WMView *view, xcb_generic_event_t *event);

/* Create a drag handler, associating drag event masks with dragEventProc */
void WMCreateDragHandler(WMView *view, WMEventProc *dragEventProc, void *clientData);

void WMDeleteDragHandler(WMView *view, WMEventProc *dragEventProc, void *clientData);

/* set default drag handler for view */
void WMSetViewDraggable(WMView *view, WMDragSourceProcs *procs, WMPixmap *dragImage);

void WMUnsetViewDraggable(WMView *view);

/* ---[ WINGs/dragdestination.c ]----------------------------------------- */

void WMRegisterViewForDraggedTypes(WMView *view, WMArray *acceptedTypes);

void WMUnregisterViewDraggedTypes(WMView *view);

void WMSetViewDragDestinationProcs(WMView *view, WMDragDestinationProcs *procs);

/* ---[ WINGs/wfont.c ]--------------------------------------------------- */

bool WMIsAntialiasingEnabled(WMScreen *scrPtr);

WMFont* WMCreateFont(WMScreen *scrPtr, const char *fontName);

WMFont* WMCopyFontWithStyle(WMScreen *scrPtr, WMFont *font, WMFontStyle style);

WMFont* WMRetainFont(WMFont *font);

void WMReleaseFont(WMFont *font);

char* WMGetFontName(WMFont *font);

unsigned int WMFontHeight(WMFont *font);

void WMGetScaleBaseFromSystemFont(WMScreen *scrPtr, int *alphabetWidth, int *fontHeight);

/* Exit-only, releases Pango and fontconfig caches after all screens. */
void WMShutdownFonts(void);

void WMSetWidgetDefaultFont(WMScreen *scr, WMFont *font);

void WMSetWidgetDefaultBoldFont(WMScreen *scr, WMFont *font);

WMFont* WMDefaultSystemFont(WMScreen *scrPtr);

WMFont* WMDefaultBoldSystemFont(WMScreen *scrPtr);

WMFont* WMSystemFontOfSize(WMScreen *scrPtr, int size);

WMFont* WMBoldSystemFontOfSize(WMScreen *scrPtr, int size);

void WMDrawString(WMScreen *scr, xcb_drawable_t d, WMColor *color, WMFont *font,
                  int x, int y, const char *text, int length);

void WMDrawImageString(WMScreen *scr, xcb_drawable_t d, WMColor *color,
                       WMColor *background, WMFont *font, int x, int y,
                       const char *text, int length);

int WMWidthOfString(WMFont *font, const char *text, int length);

/* Record d's size for the cairo surface acquisition path so painting
 * on d never needs a synchronous geometry read. Called from the
 * window create and resize chokepoints, callers that create Pixmaps
 * ad hoc may call it too. */
void WMScreenNoteDrawableSize(WMScreen *scr, xcb_drawable_t d,
                              unsigned width, unsigned height);

/* Same, but width/height are logical units and the cache records the
 * derived device size plus a scaled flag so WCairoBeginForDrawable()
 * on d applies cairo_scale(uiScale). */
void WMScreenNoteDrawableSizeScaled(WMScreen *scr, xcb_drawable_t d,
                                    unsigned width, unsigned height);

/* Create a screen-depth server pixmap at logical width x height scaled
 * to device size and record it as scaled, so WCairoBeginForDrawable()
 * on it works in logical units. Pair with WMScreenAbandonDrawable() +
 * wXFreePixmap(). */
xcb_pixmap_t W_CreateScaledPixmap(WMScreen *scr, unsigned int width, unsigned int height);

/* Drop any per-screen render state pointing at d before the caller
 * destroys d, so the next paint does not reuse a stale cache entry. */
void WMScreenAbandonDrawable(WMScreen *scr, xcb_drawable_t d);

/* Detach every cached WMFont for this screen so the next WMCreateFont re-opens
 * under the current antialiasedText flag. Holders keep valid references. */
void WMFlushFontCache(WMScreen *scr);

/* Re-read the WMGLOBAL domain (SystemFont, AntialiasedText, ...). */
void WMReloadConfiguration(void);

/* Current antialiasedText flag (re-read by WMReloadConfiguration). */
bool WMGetAntialiasedText(void);

/* Configured WINGs system font from WMGLOBAL, the built-in "sans serif"
 * when the key is not set. */
const char *W_getconf_systemFont(void);

/* Configured WINGs system colors from WMGLOBAL (X11 color name or
 * #rrggbb), NULL when the key is not set. The built-in fallbacks apply
 * then: gray #AEAAAE, dark gray #515551, tab light #D9D9D9, tab
 * #848484, selection #bbbbcc, grid #cccccc. */
const char *W_getconf_defaultBackColor(void);
const char *W_getconf_darkGrayColor(void);
const char *W_getconf_tabColor(void);
const char *W_getconf_tabLightColor(void);
const char *W_getconf_selectionColor(void);
const char *W_getconf_gridColor(void);
const char *W_getconf_textFieldBackColor(void);
const char *W_getconf_selectedBackColor(void);

/* Re-apply the configured gray/darkGray system colors to this screen in
 * place when the WMGLOBAL color keys changed since they were last
 * applied. Call this after WMReloadConfiguration(). Colors not yet
 * allocated on the screen pick the new values when they are. Returns
 * true when a refresh was applied (the caller should repaint). */
bool WMRefreshSystemColors(WMScreen *scr);

/* ---[ WINGs/wpixmap.c ]------------------------------------------------- */

WMPixmap* WMRetainPixmap(WMPixmap *pixmap);

void WMReleasePixmap(WMPixmap *pixmap);

WMPixmap* WMCreatePixmap(WMScreen *scrPtr, int width, int height, int depth,
                         bool masked);

WMPixmap* WMCreatePixmapFromXPixmaps(WMScreen *scrPtr, xcb_pixmap_t pixmap,
                                     xcb_pixmap_t mask, int width, int height,
                                     int depth);

WMPixmap* WMCreatePixmapFromRImage(WMScreen *scrPtr, RImage *image,
                                   int threshold);

WMPixmap* WMCreatePixmapFromXPMData(WMScreen *scrPtr, char **data);

WMSize WMGetPixmapSize(WMPixmap *pixmap);

WMPixmap* WMCreatePixmapFromFile(WMScreen *scrPtr, const char *fileName);

WMPixmap* WMCreateBlendedPixmapFromRImage(WMScreen *scrPtr, RImage *image,
                                          const RColor *color);

WMPixmap* WMCreateBlendedPixmapFromFile(WMScreen *scrPtr, const char *fileName,
                                        const RColor *color);

WMPixmap* WMCreateScaledBlendedPixmapFromFile(WMScreen *scrPtr, const char *fileName,
                                              const RColor *color,
                                              unsigned int width,
                                              unsigned int height);

void WMDrawPixmap(WMPixmap *pixmap, xcb_drawable_t d, int x, int y);

xcb_pixmap_t WMGetPixmapXID(WMPixmap *pixmap);

xcb_pixmap_t WMGetPixmapMaskXID(WMPixmap *pixmap);

WMPixmap* WMGetSystemPixmap(WMScreen *scr, int image);

/* ---[ WINGs/wcolor.c ]-------------------------------------------------- */


WMColor* WMDarkGrayColor(WMScreen *scr);

WMColor* WMGrayColor(WMScreen *scr);

WMColor* WMBlackColor(WMScreen *scr);

WMColor* WMWhiteColor(WMScreen *scr);

WMPixel WMColorPixel(WMColor *color);

void WMPaintColorSwatch(WMColor *color, xcb_drawable_t d, int x, int y,
                        unsigned int width, unsigned int height);

void WMReleaseColor(WMColor *color);

WMColor* WMRetainColor(WMColor *color);

WMColor* WMCreateRGBColor(WMScreen *scr, unsigned short red,
                          unsigned short green, unsigned short blue,
                          bool exact);

WMColor* WMCreateRGBAColor(WMScreen *scr, unsigned short red,
                           unsigned short green, unsigned short blue,
                           unsigned short alpha, bool exact);

WMColor* WMCreateNamedColor(WMScreen *scr, const char *name, bool exact);

RColor WMGetRColorFromColor(WMColor *color);

void WMSetColorAlpha(WMColor *color, unsigned short alpha);

unsigned short WMRedComponentOfColor(WMColor *color);

unsigned short WMGreenComponentOfColor(WMColor *color);

unsigned short WMBlueComponentOfColor(WMColor *color);

unsigned short WMGetColorAlpha(WMColor *color);

char* WMGetColorRGBDescription(WMColor *color);

/* ---[ WINGs/widgets.c ]------------------------------------------------- */

WMScreen* WMWidgetScreen(WMWidget *w);

unsigned int WMScreenWidth(WMScreen *scr);

unsigned int WMScreenHeight(WMScreen *scr);

void WMUnmapWidget(WMWidget *w);

void WMMapWidget(WMWidget *w);

bool WMWidgetIsMapped(WMWidget *w);

void WMRaiseWidget(WMWidget *w);

void WMLowerWidget(WMWidget *w);

void WMMoveWidget(WMWidget *w, int x, int y);

void WMResizeWidget(WMWidget *w, unsigned int width, unsigned int height);

void WMSetWidgetBackgroundColor(WMWidget *w, WMColor *color);

WMColor* WMGetWidgetBackgroundColor(WMWidget *w);

void WMSetWidgetBackgroundPixmap(WMWidget *w, WMPixmap *pix);

WMPixmap *WMGetWidgetBackgroundPixmap(WMWidget *w);

void WMMapSubwidgets(WMWidget *w);

void WMUnmapSubwidgets(WMWidget *w);

void WMRealizeWidget(WMWidget *w);

void WMReparentWidget(WMWidget *w, WMWidget *newParent, int x, int y);

void WMDestroyWidget(WMWidget *widget);

void WMHangData(WMWidget *widget, void *data);

void* WMGetHangedData(WMWidget *widget);

unsigned int WMWidgetWidth(WMWidget *w);

unsigned int WMWidgetHeight(WMWidget *w);

xcb_window_t WMWidgetXID(WMWidget *w);

void WMRedisplayWidget(WMWidget *w);

/* ---[ WINGs/wview.c ]--------------------------------------------------- */

xcb_window_t WMViewXID(WMView *view);

void WMSetViewNotifySizeChanges(WMView *view, bool flag);

void WMSetViewExpandsToParent(WMView *view, int topOffs, int leftOffs,
                              int rightOffs, int bottomOffs);

WMSize WMGetViewSize(WMView *view);

WMPoint WMGetViewPosition(WMView *view);

WMPoint WMGetViewScreenPosition(WMView *view);

WMWidget* WMWidgetOfView(WMView *view);

/* notifications */
extern const char *WMViewSizeDidChangeNotification;

extern const char *WMViewFocusDidChangeNotification;

extern const char *WMViewRealizedNotification;

/* ---[ WINGs/wballoon.c ]------------------------------------------------ */

void WMSetBalloonTextForView(const char *text, WMView *view);

void WMSetBalloonTextAlignment(WMScreen *scr, WMAlignment alignment);

void WMSetBalloonFont(WMScreen *scr, WMFont *font);

void WMSetBalloonTextColor(WMScreen *scr, WMColor *color);

void WMSetBalloonDelay(WMScreen *scr, int delay);

void WMSetBalloonEnabled(WMScreen *scr, bool flag);


/* ---[ WINGs/wwindow.c ]------------------------------------------------- */

WMWindow* WMCreateWindow(WMScreen *screen, const char *name);

WMWindow* WMCreateWindowWithStyle(WMScreen *screen, const char *name, int style);

WMWindow* WMCreatePanelWithStyleForWindow(WMWindow *owner, const char *name,
                                          int style);

WMWindow* WMCreatePanelForWindow(WMWindow *owner, const char *name);

void WMChangePanelOwner(WMWindow *win, WMWindow *newOwner);

void WMSetWindowTitle(WMWindow *wPtr, const char *title);

void WMSetWindowMiniwindowTitle(WMWindow *win, const char *title);

void WMSetWindowMiniwindowImage(WMWindow *win, RImage *image);

void WMSetWindowMiniwindowPixmap(WMWindow *win, WMPixmap *pixmap);

void WMSetWindowCloseAction(WMWindow *win, WMAction *action, void *clientData);

void WMSetWindowInitialPosition(WMWindow *win, int x, int y);

void WMSetWindowUserPosition(WMWindow *win, int x, int y);

void WMSetWindowAspectRatio(WMWindow *win, int minX, int minY,
                            int maxX, int maxY);

void WMSetWindowMaxSize(WMWindow *win, unsigned width, unsigned height);

void WMSetWindowMinSize(WMWindow *win, unsigned width, unsigned height);

void WMSetWindowBaseSize(WMWindow *win, unsigned width, unsigned height);

void WMSetWindowResizeIncrements(WMWindow *win, unsigned wIncr, unsigned hIncr);

void WMSetWindowLevel(WMWindow *win, int level);

void WMSetWindowDocumentEdited(WMWindow *win, bool flag);

void WMCloseWindow(WMWindow *win);

/* ---[ WINGs/wbutton.c ]------------------------------------------------- */

void WMSetButtonAction(WMButton *bPtr, WMAction *action, void *clientData);

#define WMCreateCommandButton(parent) \
    WMCreateCustomButton((parent), WBBSpringLoadedMask\
    |WBBPushInMask\
    |WBBPushLightMask\
    |WBBPushChangeMask)

#define WMCreateRadioButton(parent) \
    WMCreateButton((parent), WBTRadio)

#define WMCreateSwitchButton(parent) \
    WMCreateButton((parent), WBTSwitch)

WMButton* WMCreateButton(WMWidget *parent, WMButtonType type);

WMButton* WMCreateCustomButton(WMWidget *parent, int behaviourMask);

void WMSetButtonImageDefault(WMButton *bPtr);

void WMSetButtonImage(WMButton *bPtr, WMPixmap *image);

void WMSetButtonAltImage(WMButton *bPtr, WMPixmap *image);

void WMSetButtonImagePosition(WMButton *bPtr, WMImagePosition position);

void WMSetButtonFont(WMButton *bPtr, WMFont *font);

void WMSetButtonTextAlignment(WMButton *bPtr, WMAlignment alignment);

void WMSetButtonText(WMButton *bPtr, const char *text);

/* Returns direct pointer to internal data, do not modify! */
const char *WMGetButtonText(WMButton *bPtr);

void WMSetButtonAltText(WMButton *bPtr, const char *text);

void WMSetButtonTextColor(WMButton *bPtr, WMColor *color);

void WMSetButtonAltTextColor(WMButton *bPtr, WMColor *color);

void WMSetButtonDisabledTextColor(WMButton *bPtr, WMColor *color);

void WMSetButtonSelected(WMButton *bPtr, int isSelected);

int WMGetButtonSelected(WMButton *bPtr);

void WMSetButtonBordered(WMButton *bPtr, int isBordered);

void WMSetButtonEnabled(WMButton *bPtr, bool flag);

int WMGetButtonEnabled(WMButton *bPtr);

void WMSetButtonImageDimsWhenDisabled(WMButton *bPtr, bool flag);

void WMSetButtonTag(WMButton *bPtr, int tag);

void WMGroupButtons(WMButton *bPtr, WMButton *newMember);

void WMPerformButtonClick(WMButton *bPtr);

void WMSetButtonContinuous(WMButton *bPtr, bool flag);

void WMSetButtonPeriodicDelay(WMButton *bPtr, float delay, float interval);

/* ---[ WINGs/wlabel.c ]-------------------------------------------------- */

WMLabel* WMCreateLabel(WMWidget *parent);

void WMSetLabelWraps(WMLabel *lPtr, bool flag);

void WMSetLabelImage(WMLabel *lPtr, WMPixmap *image);

WMPixmap* WMGetLabelImage(WMLabel *lPtr);

char* WMGetLabelText(WMLabel *lPtr);

void WMSetLabelImagePosition(WMLabel *lPtr, WMImagePosition position);

void WMSetLabelTextAlignment(WMLabel *lPtr, WMAlignment alignment);

void WMSetLabelRelief(WMLabel *lPtr, WMReliefType relief);

void WMSetLabelText(WMLabel *lPtr, const char *text);

WMFont* WMGetLabelFont(WMLabel *lPtr);

void WMSetLabelFont(WMLabel *lPtr, WMFont *font);

void WMSetLabelTextColor(WMLabel *lPtr, WMColor *color);

/*! In markup mode the label's caption is parsed as Pango markup
 *  (<b>, <i>, <span foreground=...>, ...). Any non-literal string must be
 *  escaped with g_markup_escape_text() before being set. */
void WMSetLabelUseMarkup(WMLabel *lPtr, bool flag);

bool WMGetLabelUseMarkup(WMLabel *lPtr);

/*! Equivalent to WMSetLabelUseMarkup(lPtr, True) followed by
 *  WMSetLabelText(lPtr, markup). WMGetLabelText() still returns the raw
 *  stored string including tags. Passing NULL clears the label. */
void WMSetLabelMarkup(WMLabel *lPtr, const char *markup);

/* ---[ WINGs/wframe.c ]-------------------------------------------------- */

WMFrame* WMCreateFrame(WMWidget *parent);

void WMSetFrameTitlePosition(WMFrame *fPtr, WMTitlePosition position);

void WMSetFrameRelief(WMFrame *fPtr, WMReliefType relief);

void WMSetFrameTitle(WMFrame *fPtr, const char *title);

void WMSetFrameTitleColor(WMFrame *fPtr, WMColor *color);

/* ---[ WINGs/wtextfield.c ]---------------------------------------------- */

WMTextField* WMCreateTextField(WMWidget *parent);

void WMInsertTextFieldText(WMTextField *tPtr, const char *text, int position);

void WMDeleteTextFieldRange(WMTextField *tPtr, WMRange range);

/* you can free the returned string */
char* WMGetTextFieldText(WMTextField *tPtr);

void WMSetTextFieldText(WMTextField *tPtr, const char *text);

/* hint drawn in a dim color while the field is empty and unfilled,
 * disappears as soon as the user types */
void WMSetTextFieldPlaceholder(WMTextField *tPtr, const char *text);

void WMSetTextFieldAlignment(WMTextField *tPtr, WMAlignment alignment);

void WMSetTextFieldFont(WMTextField *tPtr, WMFont *font);

WMFont* WMGetTextFieldFont(WMTextField *tPtr);

void WMSetTextFieldBordered(WMTextField *tPtr, bool bordered);

void WMSetTextFieldBeveled(WMTextField *tPtr, bool flag);

bool WMGetTextFieldEditable(WMTextField *tPtr);

void WMSetTextFieldEditable(WMTextField *tPtr, bool flag);

void WMSetTextFieldSecure(WMTextField *tPtr, bool flag);

void WMSelectTextFieldRange(WMTextField *tPtr, WMRange range);

void WMSetTextFieldCursorPosition(WMTextField *tPtr, unsigned int position);

unsigned WMGetTextFieldCursorPosition(WMTextField *tPtr);

void WMSetTextFieldNextTextField(WMTextField *tPtr, WMTextField *next);

void WMSetTextFieldPrevTextField(WMTextField *tPtr, WMTextField *prev);

void WMSetTextFieldDelegate(WMTextField *tPtr, WMTextFieldDelegate *delegate);

WMTextFieldDelegate* WMGetTextFieldDelegate(WMTextField *tPtr);

extern const char *WMTextDidChangeNotification;
extern const char *WMTextDidBeginEditingNotification;
extern const char *WMTextDidEndEditingNotification;

/* ---[ WINGs/wscroller.c ]----------------------------------------------- */

WMScroller* WMCreateScroller(WMWidget *parent);

void WMSetScrollerParameters(WMScroller *sPtr, float floatValue,
                             float knobProportion);

float WMGetScrollerKnobProportion(WMScroller *sPtr);

float WMGetScrollerValue(WMScroller *sPtr);

WMScrollerPart WMGetScrollerHitPart(WMScroller *sPtr);

void WMSetScrollerAction(WMScroller *sPtr, WMAction *action, void *clientData);

void WMSetScrollerArrowsPosition(WMScroller *sPtr,
                                 WMScrollArrowPosition position);

extern const char *WMScrollerDidScrollNotification;

/* ---[ WINGs/wlist.c ]--------------------------------------------------- */

WMList* WMCreateList(WMWidget *parent);

void WMSetListAllowMultipleSelection(WMList *lPtr, bool flag);

void WMSetListAllowEmptySelection(WMList *lPtr, bool flag);

#define WMAddListItem(lPtr, text) WMInsertListItem((lPtr), -1, (text))

WMListItem* WMInsertListItem(WMList *lPtr, int row, const char *text);

void WMSortListItems(WMList *lPtr);

void WMSortListItemsWithComparer(WMList *lPtr, WMCompareDataProc *func);

int WMFindRowOfListItemWithTitle(WMList *lPtr, const char *title);

WMListItem* WMGetListItem(WMList *lPtr, int row);

WMArray* WMGetListItems(WMList *lPtr);

void WMRemoveListItem(WMList *lPtr, int row);

void WMSelectListItem(WMList *lPtr, int row);

void WMUnselectListItem(WMList *lPtr, int row);

/* This will select all items in range, and deselect all the others */
void WMSetListSelectionToRange(WMList *lPtr, WMRange range);

/* This will select all items in range, leaving the others as they are */
void WMSelectListItemsInRange(WMList *lPtr, WMRange range);

void WMSelectAllListItems(WMList *lPtr);

void WMUnselectAllListItems(WMList *lPtr);

void WMSetListUserDrawProc(WMList *lPtr, WMListDrawProc *proc);

/* Background this list paints for a selected row (the WMGLOBAL
 * SelectedBackColor, refreshed in place). Borrowed reference. */
WMColor *WMGetListSelectionColor(WMList *lPtr);

void WMSetListUserDrawItemHeight(WMList *lPtr, unsigned short height);

int WMGetListItemHeight(WMList *lPtr);

/* don't free the returned data */
WMArray* WMGetListSelectedItems(WMList *lPtr);

/*
 * For the following 2 functions, in case WMList allows multiple selection,
 * the first item in the list of selected items, respective its row number,
 * will be returned.
 */

/* don't free the returned data */
WMListItem* WMGetListSelectedItem(WMList *lPtr);

int WMGetListSelectedItemRow(WMList *lPtr);

void WMSetListAction(WMList *lPtr, WMAction *action, void *clientData);

void WMSetListDoubleAction(WMList *lPtr, WMAction *action, void *clientData);

void WMClearList(WMList *lPtr);

int WMGetListNumberOfRows(WMList *lPtr);

void WMSetListPosition(WMList *lPtr, int row);

void WMSetListBottomPosition(WMList *lPtr, int row);

int WMGetListPosition(WMList *lPtr);

bool WMListAllowsMultipleSelection(WMList *lPtr);

bool WMListAllowsEmptySelection(WMList *lPtr);


extern const char *WMListDidScrollNotification;
extern const char *WMListSelectionDidChangeNotification;

/* ---[ WINGs/wbrowser.c ]------------------------------------------------ */

WMBrowser* WMCreateBrowser(WMWidget *parent);

void WMSetBrowserAllowMultipleSelection(WMBrowser *bPtr, bool flag);

void WMSetBrowserAllowEmptySelection(WMBrowser *bPtr, bool flag);

void WMSetBrowserPathSeparator(WMBrowser *bPtr, const char *separator);

void WMSetBrowserTitled(WMBrowser *bPtr, bool flag);

void WMLoadBrowserColumnZero(WMBrowser *bPtr);

int WMAddBrowserColumn(WMBrowser *bPtr);

void WMRemoveBrowserItem(WMBrowser *bPtr, int column, int row);

void WMSetBrowserMaxVisibleColumns(WMBrowser *bPtr, int columns);

void WMSetBrowserColumnTitle(WMBrowser *bPtr, int column, const char *title);

WMListItem* WMInsertBrowserItem(WMBrowser *bPtr, int column, int row, const char *text, bool isBranch);

void WMSortBrowserColumn(WMBrowser *bPtr, int column);

void WMSortBrowserColumnWithComparer(WMBrowser *bPtr, int column,
                                     WMCompareDataProc *func);

/* Don't free the returned string. */
const char* WMSetBrowserPath(WMBrowser *bPtr, const char *path);

/* free the returned string */
char* WMGetBrowserPath(WMBrowser *bPtr);

/* free the returned string */
char* WMGetBrowserPathToColumn(WMBrowser *bPtr, int column);

/* free the returned array */
WMArray* WMGetBrowserPaths(WMBrowser *bPtr);

void WMSetBrowserAction(WMBrowser *bPtr, WMAction *action, void *clientData);

void WMSetBrowserDoubleAction(WMBrowser *bPtr, WMAction *action,
                              void *clientData);

WMListItem* WMGetBrowserSelectedItemInColumn(WMBrowser *bPtr, int column);

int WMGetBrowserFirstVisibleColumn(WMBrowser *bPtr);

int WMGetBrowserSelectedColumn(WMBrowser *bPtr);

int WMGetBrowserSelectedRowInColumn(WMBrowser *bPtr, int column);

int WMGetBrowserNumberOfColumns(WMBrowser *bPtr);

int WMGetBrowserMaxVisibleColumns(WMBrowser *bPtr);

WMList* WMGetBrowserListInColumn(WMBrowser *bPtr, int column);

void WMSetBrowserDelegate(WMBrowser *bPtr, WMBrowserDelegate *delegate);

bool WMBrowserAllowsMultipleSelection(WMBrowser *bPtr);

bool WMBrowserAllowsEmptySelection(WMBrowser *bPtr);

void WMSetBrowserHasScroller(WMBrowser *bPtr, int hasScroller);

/* ---[ WINGs/wmenuitem.c ]----------------------------------------------- */


bool WMMenuItemIsSeparator(WMMenuItem *item);

WMMenuItem* WMCreateMenuItem(void);

void WMDestroyMenuItem(WMMenuItem *item);

bool WMGetMenuItemEnabled(WMMenuItem *item);

void WMSetMenuItemEnabled(WMMenuItem *item, bool flag);

char* WMGetMenuItemShortcut(WMMenuItem *item);

unsigned WMGetMenuItemShortcutModifierMask(WMMenuItem *item);

void WMSetMenuItemShortcut(WMMenuItem *item, const char *shortcut);

void WMSetMenuItemShortcutModifierMask(WMMenuItem *item, unsigned mask);

void* WMGetMenuItemRepresentedObject(WMMenuItem *item);

void WMSetMenuItemRepresentedObject(WMMenuItem *item, void *object);

void WMSetMenuItemAction(WMMenuItem *item, WMAction *action, void *data);

WMAction* WMGetMenuItemAction(WMMenuItem *item);

void* WMGetMenuItemData(WMMenuItem *item);

void WMSetMenuItemTitle(WMMenuItem *item, const char *title);

char* WMGetMenuItemTitle(WMMenuItem *item);

void WMSetMenuItemState(WMMenuItem *item, int state);

int WMGetMenuItemState(WMMenuItem *item);

void WMSetMenuItemPixmap(WMMenuItem *item, WMPixmap *pixmap);

WMPixmap* WMGetMenuItemPixmap(WMMenuItem *item);

void WMSetMenuItemOnStatePixmap(WMMenuItem *item, WMPixmap *pixmap);

WMPixmap* WMGetMenuItemOnStatePixmap(WMMenuItem *item);

void WMSetMenuItemOffStatePixmap(WMMenuItem *item, WMPixmap *pixmap);

WMPixmap* WMGetMenuItemOffStatePixmap(WMMenuItem *item);

void WMSetMenuItemMixedStatePixmap(WMMenuItem *item, WMPixmap *pixmap);

WMPixmap* WMGetMenuItemMixedStatePixmap(WMMenuItem *item);

/* ---[ WINGs/wpopupbutton.c ]-------------------------------------------- */

WMPopUpButton* WMCreatePopUpButton(WMWidget *parent);

void WMSetPopUpButtonAction(WMPopUpButton *sPtr, WMAction *action,
                            void *clientData);

void WMSetPopUpButtonPullsDown(WMPopUpButton *bPtr, bool flag);

void WMSetPopUpButtonMenuAutoWidth(WMPopUpButton *bPtr, bool flag);

WMMenuItem* WMAddPopUpButtonItem(WMPopUpButton *bPtr, const char *title);

WMMenuItem* WMInsertPopUpButtonItem(WMPopUpButton *bPtr, int index,
                                    const char *title);

void WMRemovePopUpButtonItem(WMPopUpButton *bPtr, int index);

void WMSetPopUpButtonItemEnabled(WMPopUpButton *bPtr, int index, bool flag);

bool WMGetPopUpButtonItemEnabled(WMPopUpButton *bPtr, int index);

void WMSetPopUpButtonSelectedItem(WMPopUpButton *bPtr, int index);

int WMGetPopUpButtonSelectedItem(WMPopUpButton *bPtr);

void WMSetPopUpButtonText(WMPopUpButton *bPtr, const char *text);

/* don't free the returned data */
char* WMGetPopUpButtonItem(WMPopUpButton *bPtr, int index);

WMMenuItem* WMGetPopUpButtonMenuItem(WMPopUpButton *bPtr, int index);

int WMGetPopUpButtonNumberOfItems(WMPopUpButton *bPtr);

void WMSetPopUpButtonEnabled(WMPopUpButton *bPtr, bool flag);

bool WMGetPopUpButtonEnabled(WMPopUpButton *bPtr);

int WMSelectPopUpButtonPreviousItem(WMPopUpButton *bPtr);

int WMSelectPopUpButtonNextItem(WMPopUpButton *bPtr);

/* ---[ WINGs/wmultiselectpopup.c ]-------------------------------------- */

WMMultiSelectPopUpButton *WMCreateMultiSelectPopUpButton(WMWidget *parent);

void WMSetMultiSelectPopUpButtonAction(WMMultiSelectPopUpButton *bPtr,
					WMAction *action, void *clientData);

WMMenuItem *WMAddMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr,
					    const char *title);

void WMRemoveMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index);

void WMRemoveAllMultiSelectPopUpButtonItems(WMMultiSelectPopUpButton *bPtr);

void WMSetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr,
					    int index, bool flag);

bool WMGetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr, int index);

int WMGetMultiSelectPopUpButtonNumberOfItems(WMMultiSelectPopUpButton *bPtr);

/* don't free the returned data */
char *WMGetMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index);

WMMenuItem *WMGetMultiSelectPopUpButtonMenuItem(WMMultiSelectPopUpButton *bPtr,
						int index);

void WMSetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr, bool flag);

bool WMGetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr);

void WMSetMultiSelectPopUpButtonCaption(WMMultiSelectPopUpButton *bPtr,
					const char *text);

/* bit N of the mask corresponds to item N (1 = selected, 0 = unselected),
 * capped at the 64 bits a uint64_t can hold, so items beyond 64 are
 * ignored by the setter and never set by the getter */
void WMSetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr,
						uint64_t mask);

uint64_t WMGetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr);

/* ---[ WINGs/wprogressindicator.c ]------------------------------------- */

WMProgressIndicator* WMCreateProgressIndicator(WMWidget *parent);

void WMSetProgressIndicatorMinValue(WMProgressIndicator *progressindicator, int value);

void WMSetProgressIndicatorMaxValue(WMProgressIndicator *progressindicator, int value);

void WMSetProgressIndicatorValue(WMProgressIndicator *progressindicator, int value);

int WMGetProgressIndicatorMinValue(WMProgressIndicator *progressindicator);

int WMGetProgressIndicatorMaxValue(WMProgressIndicator *progressindicator);

int WMGetProgressIndicatorValue(WMProgressIndicator *progressindicator);

/* ---[ WINGs/wcolorpanel.c ]--------------------------------------------- */

WMColorPanel* WMGetColorPanel(WMScreen *scrPtr);

void WMFreeColorPanel(WMColorPanel *panel);

void WMShowColorPanel(WMColorPanel *panel);

void WMCloseColorPanel(WMColorPanel *panel);

void WMSetColorPanelColor(WMColorPanel *panel, WMColor *color);

WMColor* WMGetColorPanelColor(WMColorPanel *panel);

void WMSetColorPanelPickerMode(WMColorPanel *panel, WMColorPanelMode mode);

void WMSetColorPanelAction(WMColorPanel *panel, WMAction2 *action, void *data);

extern const char *WMColorPanelColorChangedNotification;

/* ---[ WINGs/wcolorwell.c ]---------------------------------------------- */

WMColorWell* WMCreateColorWell(WMWidget *parent);

void WMSetColorWellColor(WMColorWell *cPtr, WMColor *color);

WMColor* WMGetColorWellColor(WMColorWell *cPtr);

void WSetColorWellBordered(WMColorWell *cPtr, bool flag);


extern const char *WMColorWellDidChangeNotification;


/* ---[ WINGs/wscrollview.c ]--------------------------------------------- */

WMScrollView* WMCreateScrollView(WMWidget *parent);

void WMResizeScrollViewContent(WMScrollView *sPtr, unsigned int width,
                               unsigned int height);

void WMSetScrollViewHasHorizontalScroller(WMScrollView *sPtr, bool flag);

void WMSetScrollViewHasVerticalScroller(WMScrollView *sPtr, bool flag);

void WMSetScrollViewContentView(WMScrollView *sPtr, WMView *view);

void WMSetScrollViewRelief(WMScrollView *sPtr, WMReliefType type);

WMRect WMGetScrollViewVisibleRect(WMScrollView *sPtr);

void WMScrollViewScrollPoint(WMScrollView * sPtr, WMPoint point);

WMScroller* WMGetScrollViewHorizontalScroller(WMScrollView *sPtr);

WMScroller* WMGetScrollViewVerticalScroller(WMScrollView *sPtr);

void WMSetScrollViewLineScroll(WMScrollView *sPtr, int amount);

void WMSetScrollViewPageScroll(WMScrollView *sPtr, int amount);

/* ---[ WINGs/wslider.c ]------------------------------------------------- */

WMSlider* WMCreateSlider(WMWidget *parent);

int WMGetSliderMinValue(WMSlider *slider);

int WMGetSliderMaxValue(WMSlider *slider);

int WMGetSliderValue(WMSlider *slider);

void WMSetSliderMinValue(WMSlider *slider, int value);

void WMSetSliderMaxValue(WMSlider *slider, int value);

void WMSetSliderValue(WMSlider *slider, int value);

void WMSetSliderContinuous(WMSlider *slider, bool flag);

void WMSetSliderAction(WMSlider *slider, WMAction *action, void *data);

void WMSetSliderKnobThickness(WMSlider *sPtr, int thickness);

void WMSetSliderImage(WMSlider *sPtr, WMPixmap *pixmap);

void WMSetSliderEnabled(WMSlider *sPtr, bool flag);

bool WMGetSliderEnabled(WMSlider *sPtr);

/* ---[ WINGs/wsplitview.c ]---------------------------------------------- */


WMSplitView* WMCreateSplitView(WMWidget *parent);

bool WMGetSplitViewVertical(WMSplitView *sPtr);

void WMSetSplitViewVertical(WMSplitView *sPtr, bool flag);

int WMGetSplitViewSubviewsCount(WMSplitView *sPtr); /* ??? remove ??? */

WMView* WMGetSplitViewSubviewAt(WMSplitView *sPtr, int index);

/* remove the first subview == view */
void WMRemoveSplitViewSubview(WMSplitView *sPtr, WMView *view);

void WMRemoveSplitViewSubviewAt(WMSplitView *sPtr, int index);


void WMAddSplitViewSubview(WMSplitView *sPtr, WMView *subview);

void WMAdjustSplitViewSubviews(WMSplitView *sPtr);

void WMSetSplitViewConstrainProc(WMSplitView *sPtr,
                                 WMSplitViewConstrainProc *proc);

/*
void WMSetSplitViewResizeSubviewsProc(WMSplitView *sPtr,
                                      WMSplitViewResizeSubviewsProc *proc);
*/

int WMGetSplitViewDividerThickness(WMSplitView *sPtr);

/* ...................................................................... */

WMRuler* WMCreateRuler (WMWidget *parent);

WMRulerMargins* WMGetRulerMargins(WMRuler *rPtr);

void WMSetRulerMargins(WMRuler *rPtr, WMRulerMargins margins);

bool WMIsMarginEqualToMargin(WMRulerMargins *aMargin, WMRulerMargins *anotherMargin);

int WMGetGrabbedRulerMargin(WMRuler *rPtr);

int WMGetReleasedRulerMargin(WMRuler *rPtr);

int WMGetRulerOffset(WMRuler *rPtr);

void WMSetRulerOffset(WMRuler *rPtr, int pixels);

void WMSetRulerMoveAction(WMRuler *rPtr, WMAction *action, void *clientData);

void WMSetRulerReleaseAction(WMRuler *rPtr, WMAction *action, void *clientData);

/* ....................................................................... */


#define WMCreateText(parent) WMCreateTextForDocumentType \
    ((parent), (NULL), (NULL))

WMText* WMCreateTextForDocumentType(WMWidget *parent, WMAction *parser,
                                    WMAction *writer);

void WMSetTextDelegate(WMText *tPtr, WMTextDelegate *delegate);

void WMFreezeText(WMText *tPtr);

#define WMRefreshText(tPtr) WMThawText((tPtr))

void WMThawText(WMText *tPtr);

bool WMScrollText(WMText *tPtr, int amount);

bool WMPageText(WMText *tPtr, bool direction);

void WMSetTextHasHorizontalScroller(WMText *tPtr, bool shouldhave);

void WMSetTextHasVerticalScroller(WMText *tPtr, bool shouldhave);

void WMSetTextHasRuler(WMText *tPtr, bool shouldhave);

void WMShowTextRuler(WMText *tPtr, bool show);

bool WMGetTextRulerShown(WMText *tPtr);

void WMSetTextEditable(WMText *tPtr, bool editable);

int WMGetTextEditable(WMText *tPtr);

void WMSetTextUsesMonoFont(WMText *tPtr, bool mono);

bool WMGetTextUsesMonoFont(WMText *tPtr);

void WMSetTextIndentNewLines(WMText *tPtr, bool indent);

void WMSetTextIgnoresNewline(WMText *tPtr, bool ignore);

bool WMGetTextIgnoresNewline(WMText *tPtr);

void WMSetTextDefaultFont(WMText *tPtr, WMFont *font);

WMFont* WMGetTextDefaultFont(WMText *tPtr);

void WMSetTextDefaultColor(WMText *tPtr, WMColor *color);

WMColor* WMGetTextDefaultColor(WMText *tPtr);

void WMSetTextRelief(WMText *tPtr, WMReliefType relief);

void WMSetTextForegroundColor(WMText *tPtr, WMColor *color);

void WMSetTextBackgroundColor(WMText *tPtr, WMColor *color);

void WMSetTextBackgroundPixmap(WMText *tPtr, WMPixmap *pixmap);

void WMPrependTextStream(WMText *tPtr, const char *text);

void WMAppendTextStream(WMText *tPtr, const char *text);

#define WMClearText(tPtr) WMAppendTextStream \
    ((tPtr), (NULL))

/* free the text */
char* WMGetTextStream(WMText *tPtr);

/* free the text */
char* WMGetTextSelectedStream(WMText *tPtr);

/* destroy the array */
WMArray* WMGetTextObjects(WMText *tPtr);

/* destroy the array */
WMArray* WMGetTextSelectedObjects(WMText *tPtr);

void WMSetTextSelectionColor(WMText *tPtr, WMColor *color);

WMColor* WMGetTextSelectionColor(WMText *tPtr);

void WMSetTextSelectionFont(WMText *tPtr, WMFont *font);

WMFont* WMGetTextSelectionFont(WMText *tPtr);

void WMSetTextSelectionUnderlined(WMText *tPtr, int underlined);

int WMGetTextSelectionUnderlined(WMText *tPtr);

void WMSetTextAlignment(WMText *tPtr, WMAlignment alignment);

bool WMFindInTextStream(WMText *tPtr, const char *needle, bool direction,
                        bool caseSensitive);

/* Warning: replacement can be modified by the function */
bool WMReplaceTextSelection(WMText *tPtr, char *replacement);


/* parser related stuff... use only if implementing a new parser */

void* WMCreateTextBlockWithObject(WMText *tPtr, WMWidget *w, const char *description,
                                  WMColor *color, unsigned short first,
                                  unsigned short extraInfo);

void* WMCreateTextBlockWithPixmap(WMText *tPtr, WMPixmap *p, const char *description,
                                  WMColor *color, unsigned short first,
                                  unsigned short extraInfo);

void* WMCreateTextBlockWithText(WMText *tPtr, const char *text, WMFont *font,
                                WMColor *color, unsigned short first,
                                unsigned short length);

void WMSetTextBlockProperties(WMText *tPtr, void *vtb, unsigned int first,
                              unsigned int kanji, unsigned int underlined,
                              int script, WMRulerMargins *margins);

/* do NOT free the margins */
void WMGetTextBlockProperties(WMText *tPtr, void *vtb, unsigned int *first,
                              unsigned int *kanji, unsigned int *underlined,
                              int *script, WMRulerMargins *margins);

int WMGetTextInsertType(WMText *tPtr);

/*int WMGetTextBlocks(WMText *tPtr);

void WMSetCurrentTextBlock(WMText *tPtr, int current);

int WMGetCurrentTextBlock(WMText *tPtr);*/

void WMPrependTextBlock(WMText *tPtr, void *vtb);

void WMAppendTextBlock(WMText *tPtr, void *vtb);

void* WMRemoveTextBlock(WMText *tPtr);

void WMDestroyTextBlock(WMText *tPtr, void *vtb);

/* ---[ WINGs/wtabview.c ]------------------------------------------------ */

WMTabView* WMCreateTabView(WMWidget *parent);

void WMSetTabViewType(WMTabView *tPtr, WMTabViewType type);

void WMSetTabViewEnabled(WMTabView *tPtr, bool flag);

void WMSetTabViewFont(WMTabView *tPtr, WMFont *font);

void WMAddItemInTabView(WMTabView *tPtr, WMTabViewItem *item);

void WMInsertItemInTabView(WMTabView *tPtr, int index, WMTabViewItem *item);

void WMRemoveTabViewItem(WMTabView *tPtr, WMTabViewItem *item);

WMTabViewItem* WMAddTabViewItemWithView(WMTabView *tPtr, WMView *view,
                                        int identifier, const char *label);

WMTabViewItem* WMTabViewItemAtPoint(WMTabView *tPtr, int x, int y);

void WMSelectFirstTabViewItem(WMTabView *tPtr);

void WMSelectLastTabViewItem(WMTabView *tPtr);

void WMSelectNextTabViewItem(WMTabView *tPtr);

void WMSelectPreviousTabViewItem(WMTabView *tPtr);

WMTabViewItem* WMGetSelectedTabViewItem(WMTabView *tPtr);

void WMSelectTabViewItem(WMTabView *tPtr, WMTabViewItem *item);

void WMSelectTabViewItemAtIndex(WMTabView *tPtr, int index);

void WMSetTabViewDelegate(WMTabView *tPtr, WMTabViewDelegate *delegate);


WMTabViewItem* WMCreateTabViewItemWithIdentifier(int identifier);

void WMSetTabViewItemEnabled(WMTabViewItem *tPtr, bool flag);

int WMGetTabViewItemIdentifier(WMTabViewItem *item);

void WMSetTabViewItemLabel(WMTabViewItem *item, const char *label);

char* WMGetTabViewItemLabel(WMTabViewItem *item);

void WMSetTabViewItemView(WMTabViewItem *item, WMView *view);

WMView* WMGetTabViewItemView(WMTabViewItem *item);

void WMDestroyTabViewItem(WMTabViewItem *item);


/* ---[ WINGs/wbox.c ]---------------------------------------------------- */

WMBox* WMCreateBox(WMWidget *parent);

void WMSetBoxBorderWidth(WMBox *box, unsigned width);

void WMAddBoxSubview(WMBox *bPtr, WMView *view, bool expand, bool fill,
                     int minSize, int maxSize, int space);

void WMAddBoxSubviewAtEnd(WMBox *bPtr, WMView *view, bool expand, bool fill,
                          int minSize, int maxSize, int space);

void WMRemoveBoxSubview(WMBox *bPtr, WMView *view);

void WMSetBoxHorizontal(WMBox *box, bool flag);

/* ---[ WINGs/wpanel.c ]-------------------------------------------------- */

int WMRunAlertPanel(WMScreen *app, WMWindow *owner, const char *title, const char *msg,
                    const char *defaultButton, const char *alternateButton,
                    const char *otherButton);

/* you can free the returned string */
char* WMRunInputPanel(WMScreen *app, WMWindow *owner, const char *title, const char *msg,
                      const char *defaultText, const char *okButton, const char *cancelButton);

WMAlertPanel* WMCreateAlertPanel(WMScreen *app, WMWindow *owner, const char *title,
                                 const char *msg, const char *defaultButton,
                                 const char *alternateButton, const char *otherButton);

WMAlertPanel* WMCreateScaledAlertPanel(WMScreen *app, WMWindow *owner, const char *title,
                                       const char *msg, const char *defaultButton,
                                       const char *alternateButton, const char *otherButton);

WMInputPanel* WMCreateInputPanel(WMScreen *app, WMWindow *owner, const char *title,
                                 const char *msg, const char *defaultText, const char *okButton,
                                 const char *cancelButton);

WMInputPanel* WMCreateScaledInputPanel(WMScreen *app, WMWindow *owner, const char *title,
                                       const char *msg, const char *defaultText, const char *okButton,
                                       const char *cancelButton);

WMGenericPanel* WMCreateGenericPanel(WMScreen *scrPtr, WMWindow *owner,
                                     const char *title, const char *defaultButton,
                                     const char *alternateButton);

void WMDestroyAlertPanel(WMAlertPanel *panel);

void WMDestroyInputPanel(WMInputPanel *panel);

void WMDestroyGenericPanel(WMGenericPanel *panel);

/* ---[ WINGs/wfilepanel.c ]---------------------------------------------- */

/* only 1 instance per WMScreen */
WMOpenPanel* WMGetOpenPanel(WMScreen *scrPtr);

WMSavePanel* WMGetSavePanel(WMScreen *scrPtr);

void WMSetFilePanelCanChooseDirectories(WMFilePanel *panel, bool flag);

void WMSetFilePanelCanChooseFiles(WMFilePanel *panel, bool flag);

void WMSetFilePanelAutoCompletion(WMFilePanel *panel, bool flag);

void WMSetFilePanelDirectory(WMFilePanel *panel, const char *path);

/* you can free the returned string */
char* WMGetFilePanelFileName(WMFilePanel *panel);

void WMFreeFilePanel(WMFilePanel *panel);

int WMRunModalFilePanelForDirectory(WMFilePanel *panel, WMWindow *owner,
                                    const char *path, const char *name, char **fileTypes);

void WMSetFilePanelAccessoryView(WMFilePanel *panel, WMView *view);

WMView* WMGetFilePanelAccessoryView(WMFilePanel *panel);


/* ---[ WINGs/wfontpanel.c ]---------------------------------------------- */

/* only 1 instance per WMScreen */
WMFontPanel* WMGetFontPanel(WMScreen *scr);

void WMShowFontPanel(WMFontPanel *panel);

void WMHideFontPanel(WMFontPanel *panel);

void WMFreeFontPanel(WMFontPanel *panel);

void WMSetFontPanelAction(WMFontPanel *panel, WMAction2 *action, void *data);

void WMSetFontPanelFont(WMFontPanel *panel, const char *fontName);

WMFont* WMGetFontPanelFont(WMFontPanel *panel);

/* ---[ WINGs/configuration.c ]------------------------------------------- */
unsigned W_getconf_mouseWheelUp(void);
unsigned W_getconf_mouseWheelDown(void);
void W_setconf_doubleClickDelay(int value);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif
