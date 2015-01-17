#ifndef _REMEMBER_H_
#define _REMEMBER_H_

#include <Application.h>
#include <List.h>
#include <String.h>
#include <OS.h>

typedef struct event_s {
				event_s()
					:	node(0),
						name(""),
						when(0),
						where(""),
						what("")
				{
				}

	ino_t		node;
	BString		name;

	uint32		when;
	BString		where;
	BString		what;
} Event;


class Remember : public BApplication {
public:
							Remember();
							~Remember();

		void				AddEvent(entry_ref *ref, bool watch);
		Event				*FindEvent(ino_t node);

		void				AddEvent(Event *event);
		void				RemoveEvent(Event *event);

		void				LockEvents() { acquire_sem(fEventLock); };
		void				UnlockEvents() { release_sem(fEventLock); };
		void				Notify() { release_sem(fNotify); };

		void				MessageReceived(BMessage *message);
static	int32				EventLoop(void *data);

private:
		BString				fEventDir;
		sem_id				fEventLock;
		BList				fEvents;
		BList				fAllEvents;
		sem_id				fNotify;
		thread_id			fEventLoop;
		bool				fQuiting;
};

#endif // _REMEMBER_H_
