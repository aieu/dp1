

#ifndef _MYSIGNAL_H
#define _MYSIGNAL_H


typedef	void	Sigfunc(int);	/* for signal handlers */

Sigfunc *Signal(int, Sigfunc *);

#endif
