/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2002-2024 UltraVNC Team Members. All Rights Reserved.
//  Copyright (C) 2004 Martin Scharpf, B. Braun Melsungen AG. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
//  If the source code for the program is not available from the place from
//  which you received this file, check
//  https://uvnc.com/
//
////////////////////////////////////////////////////////////////////////////


#define _CRT_NON_CONFORMING_SWPRINTFS 1

#include <windows.h>
#include <stdio.h>
#include <lmcons.h>
#include <stdlib.h>
#include <wchar.h>
#include <tchar.h>
#include <lm.h>
#include <stdio.h>
#define SECURITY_WIN32
#include <sspi.h>
#ifndef SEC_I_COMPLETE_NEEDED
#include <issperr.h>
#include <time.h>
#endif
#pragma hdrstop
#include <stdarg.h>

#define MAXLEN 256
#define MAX_PREFERRED_LENGTH    ((DWORD) -1)
#define BUFSIZE 1024

class EventLogging
{
public:
	EventLogging();
	virtual ~EventLogging();
	virtual void LogIt(WORD CategoryID, DWORD EventID, LPCTSTR *ArrayOfStrings,
		UINT NumOfArrayStr = 0,LPVOID RawData = NULL,DWORD RawDataSize = 0);
	HANDLE m_hEventLinker;
	void AddEventSourceToRegistry(LPCTSTR lpszSourceName);

};

void LOGV2(TCHAR* szMslogonLog, long EvenID, const TCHAR *format, ...);
void LOG(long EvenID, const TCHAR* format, ...);