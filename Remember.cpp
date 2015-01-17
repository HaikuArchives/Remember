/* Remember - a small tool that can remind you of things. It uses the Tracker
   as a front end. You create events by creating files with some special
   attributes. A deamon pops up an alert when the specified time has come.

   Copyright 2005 Michael Lotz, released under the MIT licence.
*/

#include <Alert.h>
#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <stdio.h>
#include "Remember.h"

#define EVENT_DIRECTORY	"Events"
#define WHEN_ATTR		"event:when"
#define WHERE_ATTR		"event:where"
#define WHAT_ATTR		"event:what"


int
main(int argc, const char *argv[])
{
	Remember app(argv[0]);
	app.Run();
	return 0;
}


static int
EventSort(const void *item1, const void *item2)
{
	return (*(Event **)item1)->when - (*(Event **)item2)->when;
}


Remember::Remember(const char *appPath)
	:	BApplication("application/x-vnd.mmlr.remember"),
		fEvents(),
		fAllEvents(),
		fEventLoop(0),
		fQuiting(false)
{
	fEventLock = create_sem(1, "event lock semaphore");
	fNotify = create_sem(0, "notify semaphore");

	fEventDir = appPath;
	fEventDir.Truncate(fEventDir.FindLast("/") + 1);
	fEventDir += EVENT_DIRECTORY;

	BDirectory directory(fEventDir.String());
	if (directory.InitCheck() == B_OK) {
		entry_ref ref;
		while (directory.GetNextRef(&ref) == B_OK)
			AddEvent(&ref, true);

		node_ref nodeRef;
		directory.GetNodeRef(&nodeRef);
		watch_node(&nodeRef, B_WATCH_DIRECTORY, be_app_messenger);
	} else
		printf("Events directory not found (\"%s\")\n", fEventDir.String());

	fEventLoop = spawn_thread(EventLoop, "EventLoop", B_NORMAL_PRIORITY, this);
	resume_thread(fEventLoop);
}


Remember::~Remember()
{
	stop_watching(be_app_messenger);

	fQuiting = true;
	Notify();

	int32 result;
	wait_for_thread(fEventLoop, &result);

	for (int32 index = 0; index < fAllEvents.CountItems(); index++)
		delete (Event *)fAllEvents.ItemAt(index);

	fAllEvents.MakeEmpty();
	fEvents.MakeEmpty();
}


void
Remember::AddEvent(entry_ref *ref, bool watch)
{
	BNode node(ref);
	Event *event = new Event();

	node_ref nodeRef;
	node.GetNodeRef(&nodeRef);
	event->node = nodeRef.node;
	event->name = ref->name;

	node.ReadAttr(WHEN_ATTR, B_TIME_TYPE, 0, &event->when, sizeof(event->when));
	node.ReadAttrString(WHERE_ATTR, &event->where);
	node.ReadAttrString(WHAT_ATTR, &event->what);

	fAllEvents.AddItem(event);
	if (watch)
		watch_node(&nodeRef, B_WATCH_ATTR, be_app_messenger);

	if (event->when > real_time_clock())
		AddEvent(event);
}


Event *
Remember::FindEvent(ino_t node)
{
	for (int32 index = 0; index < fAllEvents.CountItems(); index++) {
		Event *event = (Event *)fAllEvents.ItemAt(index);
		if (event->node == node)
			return event;
	}

	return NULL;
}


void
Remember::AddEvent(Event *event)
{
	LockEvents();
	fEvents.AddItem(event);
	UnlockEvents();
	if (fEventLoop)
		Notify();
}


void
Remember::RemoveEvent(Event *event)
{
	LockEvents();
	fEvents.RemoveItem(event);
	UnlockEvents();
}


void
Remember::MessageReceived(BMessage *message)
{
	if (message->what == B_NODE_MONITOR) {
		int32 opCode;
		ino_t node;
		message->FindInt64("node", &node);
		message->FindInt32("opcode", &opCode);

		switch (opCode) {
			case B_ENTRY_CREATED: {
				entry_ref ref;
				const char *name;

				message->FindInt32("device", &ref.device);
				message->FindInt64("directory", &ref.directory);
				message->FindString("name", &name);
				ref.set_name(name);

				AddEvent(&ref, true);
			} break;
			case B_ENTRY_REMOVED: {
				Event *event = FindEvent(node);

				if (event) {
					RemoveEvent(event);
					fAllEvents.RemoveItem(event);
					delete event;
				}
			} break;
			case B_ENTRY_MOVED: {
				Event *event = FindEvent(node);

				if (event) {
					// if we have it, it was moved away and we can remove it
					RemoveEvent(event);
					fAllEvents.RemoveItem(event);
					delete event;

					node_ref nodeRef;
					nodeRef.node = node;
					message->FindInt32("device", &nodeRef.device);
					watch_node(&nodeRef, B_STOP_WATCHING, be_app_messenger);
				} else {
					// it was moved here, build ref and add it
					entry_ref ref;
					const char *name;

					message->FindInt32("device", &ref.device);
					message->FindInt64("to directory", &ref.directory);
					message->FindString("name", &name);
					ref.set_name(name);

					AddEvent(&ref, true);
				}
			} break;
			case B_ATTR_CHANGED: {
				Event *event = FindEvent(node);
				if (!event)
					break;

				RemoveEvent(event);
				fAllEvents.RemoveItem(event);

				BString path(fEventDir);
				path << "/" << event->name;
				delete event;

				entry_ref ref;
				BEntry entry(path.String());
				entry.GetRef(&ref);
				AddEvent(&ref, false);
			} break;
		}
	}
}


int32
Remember::EventLoop(void *data)
{
	Remember *app = (Remember *)data;

	while (!app->fQuiting) {
		app->LockEvents();
		app->fEvents.SortItems(EventSort);

		while (app->fEvents.CountItems() > 0) {
			Event *event = (Event *)app->fEvents.ItemAt(0);
			if (event->when > real_time_clock())
				break;

			BString buffer = "Event Notification\n------------------\n";
			buffer << "Where:\t" << event->where << "\n";
			buffer << "What:\t\t" << event->what;
			BAlert *alert = new BAlert("Event", buffer.String(), "Delete", "Keep");
			alert->SetShortcut(0, B_DELETE);
			alert->SetShortcut(1, B_ESCAPE);
			if (alert->Go() == 0) {
				BString path(app->fEventDir);
				path << "/" << event->name;
				BEntry entry(path.String());
				entry.Remove();
			}

			app->fEvents.RemoveItem((int32)0);
		}

		bigtime_t timeout = -1;
		if (app->fEvents.CountItems() > 0) {
			Event *event = (Event *)app->fEvents.ItemAt(0);
			timeout = (event->when - real_time_clock()) * 1000000;
		}
		app->UnlockEvents();

		// wait for a specific time if there is an event or just wait if not
		if (timeout >= 0)
			acquire_sem_etc(app->fNotify, 1, B_RELATIVE_TIMEOUT, timeout);
		else
			acquire_sem(app->fNotify);
	}

	return 0;
}
