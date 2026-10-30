/*
 *  notification.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "WUtil.h"
#include "WINGsP.h"


typedef struct W_Notification {
	const char *name;
	void *object;
	void *clientData;
	int refCount;
} Notification;


const char *WMGetNotificationName(WMNotification * notification)
{
	return notification->name;
}

void *WMGetNotificationObject(WMNotification * notification)
{
	return notification->object;
}

void *WMGetNotificationClientData(WMNotification * notification)
{
	return notification->clientData;
}

WMNotification *WMCreateNotification(const char *name, void *object, void *clientData)
{
	Notification *nPtr;

	nPtr = wmalloc(sizeof(Notification));
	nPtr->name = name;
	nPtr->object = object;
	nPtr->clientData = clientData;
	nPtr->refCount = 1;

	return nPtr;
}

void WMReleaseNotification(WMNotification * notification)
{
	notification->refCount--;

	if (notification->refCount < 1) {
		wfree(notification);
	}
}

WMNotification *WMRetainNotification(WMNotification * notification)
{
	notification->refCount++;

	return notification;
}

/***************** Notification Center *****************/

typedef struct NotificationObserver {
	WMNotificationObserverAction *observerAction;
	void *observer;

	const char *name;
	void *object;

	struct NotificationObserver *prev;	/* for tables */
	struct NotificationObserver *next;
	struct NotificationObserver *nextAction;	/* for observerTable */

	/* a callback reached from WMPostNotification can remove an
	 * observer whose record the post loop has already saved as its
	 * next pointer, so freeing it there would let the loop step into
	 * released memory, when posting_depth > 0 the remove paths set
	 * dead and chain the record onto dead_list instead of freeing,
	 * and the outermost post sweeps the chain on return */
	struct NotificationObserver *nextDead;
	unsigned dead:1;
} NotificationObserver;

typedef struct W_NotificationCenter {
	WMHashTable *nameTable;	/* names -> observer lists */
	WMHashTable *objectTable;	/* object -> observer lists */
	NotificationObserver *nilList;	/* obervers that catch everything */

	WMHashTable *observerTable;	/* observer -> NotificationObserver */
} NotificationCenter;

/* default (and only) center */
static NotificationCenter *notificationCenter = NULL;

/* WMCreateArrayWithDestructor wants void(void *), WMReleaseNotification
 * takes WMNotification *, so route the call through a matching
 * signature. */
static void releaseNotificationData(void *data)
{
	WMReleaseNotification(data);
}

/* see the comment on NotificationObserver.dead */
static int posting_depth;
static NotificationObserver *dead_list;

static void release_observer(NotificationObserver *orec)
{
	if (posting_depth > 0) {
		orec->dead = 1;
		orec->nextDead = dead_list;
		dead_list = orec;
	} else {
		wfree(orec);
	}
}

void W_InitNotificationCenter(void)
{
	notificationCenter = wmalloc(sizeof(NotificationCenter));
	notificationCenter->nameTable = WMCreateHashTable(WMStringPointerHashCallbacks);
	notificationCenter->objectTable = WMCreateHashTable(WMIntHashCallbacks);
	notificationCenter->nilList = NULL;
	notificationCenter->observerTable = WMCreateHashTable(WMIntHashCallbacks);
}

void W_ReleaseNotificationCenter(void)
{
	if (notificationCenter) {
		if (notificationCenter->observerTable) {
			/* every NotificationObserver record lives in
			 * observerTable, chained per observer through
			 * nextAction, free those chains before dropping
			 * the tables that index the same records */
			WMHashEnumerator e;
			NotificationObserver *rec, *next;

			e = WMEnumerateHashTable(notificationCenter->observerTable);
			while ((rec = WMNextHashEnumeratorItem(&e)) != NULL) {
				while (rec) {
					next = rec->nextAction;
					wfree(rec);
					rec = next;
				}
			}
			WMFreeHashTable(notificationCenter->observerTable);
		}
		if (notificationCenter->nameTable)
			WMFreeHashTable(notificationCenter->nameTable);
		if (notificationCenter->objectTable)
			WMFreeHashTable(notificationCenter->objectTable);

		wfree(notificationCenter);
		notificationCenter = NULL;
	}
}

void
WMAddNotificationObserver(WMNotificationObserverAction * observerAction,
			  void *observer, const char *name, void *object)
{
	NotificationObserver *oRec, *rec;

	oRec = wmalloc(sizeof(NotificationObserver));
	oRec->observerAction = observerAction;
	oRec->observer = observer;
	oRec->name = name;
	oRec->object = object;
	oRec->next = NULL;
	oRec->prev = NULL;

	/* put this action in the list of actions for this observer */
	rec = (NotificationObserver *) WMHashInsert(notificationCenter->observerTable, observer, oRec);

	if (rec) {
		/* if this is not the first action for the observer */
		oRec->nextAction = rec;
	} else {
		oRec->nextAction = NULL;
	}

	if (!name && !object) {
		/* catch-all */
		oRec->next = notificationCenter->nilList;
		if (notificationCenter->nilList) {
			notificationCenter->nilList->prev = oRec;
		}
		notificationCenter->nilList = oRec;
	} else if (!name) {
		/* any message coming from object */
		rec = (NotificationObserver *) WMHashInsert(notificationCenter->objectTable, object, oRec);
		oRec->next = rec;
		if (rec) {
			rec->prev = oRec;
		}
	} else {
		/* name && (object || !object) */
		rec = (NotificationObserver *) WMHashInsert(notificationCenter->nameTable, name, oRec);
		oRec->next = rec;
		if (rec) {
			rec->prev = oRec;
		}
	}
}

void WMPostNotification(WMNotification * notification)
{
	NotificationObserver *orec, *tmp;

	WMRetainNotification(notification);
	posting_depth++;

	/* tell the observers that want to know about a particular message */
	orec = (NotificationObserver *) WMHashGet(notificationCenter->nameTable, notification->name);

	while (orec) {
		tmp = orec->next;

		if (!orec->dead &&
		    (!orec->object || !notification->object || orec->object == notification->object)) {
			/* tell the observer */
			if (orec->observerAction) {
				(*orec->observerAction) (orec->observer, notification);
			}
		}

		orec = tmp;
	}

	/* tell the observers that want to know about an object */
	orec = (NotificationObserver *) WMHashGet(notificationCenter->objectTable, notification->object);

	while (orec) {
		tmp = orec->next;

		/* tell the observer */
		if (!orec->dead && orec->observerAction)
			(*orec->observerAction) (orec->observer, notification);
		orec = tmp;
	}

	/* tell the catch all observers */
	orec = notificationCenter->nilList;
	while (orec) {
		tmp = orec->next;

		/* tell the observer */
		if (!orec->dead && orec->observerAction)
			(*orec->observerAction) (orec->observer, notification);
		orec = tmp;
	}

	posting_depth--;
	if (posting_depth == 0) {
		while (dead_list) {
			orec = dead_list;
			dead_list = orec->nextDead;
			wfree(orec);
		}
	}

	WMReleaseNotification(notification);
}

void WMRemoveNotificationObserver(void *observer)
{
	NotificationObserver *orec, *tmp, *rec;

	/* get the list of actions the observer is doing */
	orec = (NotificationObserver *) WMHashGet(notificationCenter->observerTable, observer);

	/*
	 * FOREACH orec IN actionlist for observer
	 * DO
	 *   remove from respective lists/tables
	 *   free
	 * END
	 */
	while (orec) {
		tmp = orec->nextAction;

		if (!orec->name && !orec->object) {
			/* catch-all */
			if (notificationCenter->nilList == orec)
				notificationCenter->nilList = orec->next;
		} else if (!orec->name) {
			/* any message coming from object */
			rec = (NotificationObserver *) WMHashGet(notificationCenter->objectTable, orec->object);
			if (rec == orec) {
				/* replace table entry */
				if (orec->next) {
					WMHashInsert(notificationCenter->objectTable, orec->object, orec->next);
				} else {
					WMHashRemove(notificationCenter->objectTable, orec->object);
				}
			}
		} else {
			/* name && (object || !object) */
			rec = (NotificationObserver *) WMHashGet(notificationCenter->nameTable, orec->name);
			if (rec == orec) {
				/* replace table entry */
				if (orec->next) {
					WMHashInsert(notificationCenter->nameTable, orec->name, orec->next);
				} else {
					WMHashRemove(notificationCenter->nameTable, orec->name);
				}
			}
		}
		if (orec->prev)
			orec->prev->next = orec->next;
		if (orec->next)
			orec->next->prev = orec->prev;

		release_observer(orec);

		orec = tmp;
	}

	WMHashRemove(notificationCenter->observerTable, observer);
}

void WMRemoveNotificationObserverWithName(void *observer, const char *name, void *object)
{
	NotificationObserver *orec, *tmp, *rec;
	NotificationObserver *newList = NULL;

	/* get the list of actions the observer is doing */
	orec = (NotificationObserver *) WMHashGet(notificationCenter->observerTable, observer);

	WMHashRemove(notificationCenter->observerTable, observer);

	/* rebuild the list of actions for the observer */

	while (orec) {
		tmp = orec->nextAction;
		if (orec->name == name && orec->object == object) {
			if (!name && !object) {
				if (notificationCenter->nilList == orec)
					notificationCenter->nilList = orec->next;
			} else if (!name) {
				rec =
				    (NotificationObserver *) WMHashGet(notificationCenter->objectTable,
								       orec->object);
				if (rec == orec) {
					assert(rec->prev == NULL);
					/* replace table entry */
					if (orec->next) {
						WMHashInsert(notificationCenter->objectTable,
							     orec->object, orec->next);
					} else {
						WMHashRemove(notificationCenter->objectTable, orec->object);
					}
				}
			} else {
				rec = (NotificationObserver *) WMHashGet(notificationCenter->nameTable,
									 orec->name);
				if (rec == orec) {
					assert(rec->prev == NULL);
					/* replace table entry */
					if (orec->next) {
						WMHashInsert(notificationCenter->nameTable,
							     orec->name, orec->next);
					} else {
						WMHashRemove(notificationCenter->nameTable, orec->name);
					}
				}
			}

			if (orec->prev)
				orec->prev->next = orec->next;
			if (orec->next)
				orec->next->prev = orec->prev;
			release_observer(orec);
		} else {
			/* append this action in the new action list */
			orec->nextAction = NULL;
			if (!newList) {
				newList = orec;
			} else {
				NotificationObserver *p;

				p = newList;
				while (p->nextAction) {
					p = p->nextAction;
				}
				p->nextAction = orec;
			}
		}
		orec = tmp;
	}

	/* reinsert the list to the table */
	if (newList) {
		WMHashInsert(notificationCenter->observerTable, observer, newList);
	}
}

void WMPostNotificationName(const char *name, void *object, void *clientData)
{
	WMNotification *notification;

	notification = WMCreateNotification(name, object, clientData);

	WMPostNotification(notification);

	WMReleaseNotification(notification);
}

/**************** Notification Queues ****************/

typedef struct W_NotificationQueue {
	WMArray *asapQueue;
	WMArray *idleQueue;

	struct W_NotificationQueue *next;
} NotificationQueue;

static WMNotificationQueue *notificationQueueList = NULL;

/* default queue */
static WMNotificationQueue *notificationQueue = NULL;

WMNotificationQueue *WMGetDefaultNotificationQueue(void)
{
	if (!notificationQueue)
		notificationQueue = WMCreateNotificationQueue();

	return notificationQueue;
}

WMNotificationQueue *WMCreateNotificationQueue(void)
{
	NotificationQueue *queue;

	queue = wmalloc(sizeof(NotificationQueue));
	queue->asapQueue = WMCreateArrayWithDestructor(8, releaseNotificationData);
	queue->idleQueue = WMCreateArrayWithDestructor(8, releaseNotificationData);
	queue->next = notificationQueueList;

	notificationQueueList = queue;

	return queue;
}

void WMEnqueueNotification(WMNotificationQueue * queue, WMNotification * notification, WMPostingStyle postingStyle)
{
	WMEnqueueCoalesceNotification(queue, notification, postingStyle, WNCOnName | WNCOnSender);
}

#define NOTIF ((WMNotification*)cdata)
#define ITEM  ((WMNotification*)item)

static int matchSenderAndName(const void *item, const void *cdata)
{
	return (NOTIF->object == ITEM->object && strcmp(NOTIF->name, ITEM->name) == 0);
}

static int matchSender(const void *item, const void *cdata)
{
	return (NOTIF->object == ITEM->object);
}

static int matchName(const void *item, const void *cdata)
{
	return (strcmp(NOTIF->name, ITEM->name) == 0);
}

#undef NOTIF
#undef ITEM

void WMDequeueNotificationMatching(WMNotificationQueue * queue, WMNotification * notification, unsigned mask)
{
	WMMatchDataProc *matchFunc;

	if ((mask & WNCOnName) && (mask & WNCOnSender))
		matchFunc = matchSenderAndName;
	else if (mask & WNCOnName)
		matchFunc = matchName;
	else if (mask & WNCOnSender)
		matchFunc = matchSender;
	else
		return;

	WMRemoveFromArrayMatching(queue->asapQueue, matchFunc, notification);
	WMRemoveFromArrayMatching(queue->idleQueue, matchFunc, notification);
}

void
WMEnqueueCoalesceNotification(WMNotificationQueue * queue,
			      WMNotification * notification, WMPostingStyle postingStyle, unsigned coalesceMask)
{
	if (coalesceMask != WNCNone)
		WMDequeueNotificationMatching(queue, notification, coalesceMask);

	switch (postingStyle) {
	case WMPostNow:
		WMPostNotification(notification);
		WMReleaseNotification(notification);
		break;

	case WMPostASAP:
		WMAddToArray(queue->asapQueue, notification);
		break;

	case WMPostWhenIdle:
		WMAddToArray(queue->idleQueue, notification);
		break;
	}
}

void W_FlushASAPNotificationQueue(void)
{
	WMNotificationQueue *queue = notificationQueueList;

	while (queue) {
		while (WMGetArrayItemCount(queue->asapQueue)) {
			WMPostNotification(WMGetFromArray(queue->asapQueue, 0));
			WMDeleteFromArray(queue->asapQueue, 0);
		}

		queue = queue->next;
	}
}

void W_FlushIdleNotificationQueue(void)
{
	WMNotificationQueue *queue = notificationQueueList;

	while (queue) {
		while (WMGetArrayItemCount(queue->idleQueue)) {
			WMPostNotification(WMGetFromArray(queue->idleQueue, 0));
			WMDeleteFromArray(queue->idleQueue, 0);
		}

		queue = queue->next;
	}
}
