/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2002-2024 UltraVNC Team Members. All Rights Reserved.
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

#include "stdhdrs.h"
#include "vncviewer.h"
#include "TextChat.h"
#include "Exception.h"
#include "commctrl.h"
#include "richedit.h"
#include "common/win32_helpers.h"
#include <rdr/Exception.h>
#include "UltraVNCHelperFunctions.h"

using namespace helper;

#define TEXTMAXSIZE 16384
#define MAXNAMESIZE	128 // MAX_COMPUTERNAME_LENGTH+1 (32)
#define CHAT_OPEN  -1 // Todo; put these codes in rfbproto.h
#define CHAT_CLOSE -2
#define CHAT_FINISHED -3

// [v1.0.2-jp1 fix]
LRESULT CALLBACK SBProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LONG_PTR pDefSBProc;
extern HINSTANCE m_hInstResDLL;

extern char sz_E1[64];
extern char sz_E2[64];

//
//
//
TextChat::TextChat(VNCviewerApp *pApp, ClientConnection *pCC)
{
	m_pApp	= pApp;
	m_pCC	= pCC;
	m_fTextChatRunning = false;
	//test m_fVisible always false
	m_fVisible = false;
	
	m_fPersistentTexts = false;
	m_szLocalText = new char [TEXTMAXSIZE];
	memset(m_szLocalText, 0, TEXTMAXSIZE);
	m_szLastLocalText = new char [TEXTMAXSIZE];
	memset(m_szLastLocalText, 0, TEXTMAXSIZE);
	m_szRemoteText = new char [TEXTMAXSIZE];
	memset(m_szRemoteText, 0, TEXTMAXSIZE);
	m_szRemoteName = new char[MAXNAMESIZE];
	memset(m_szRemoteName,0,MAXNAMESIZE);
	m_szLocalName =  new char[MAXNAMESIZE];
	memset(m_szLocalName,0,MAXNAMESIZE);

	m_szTextBoxBuffer = new char[TEXTMAXSIZE]; // History Text (containing all chat messages from everybody)
	memset(m_szTextBoxBuffer,0,TEXTMAXSIZE);

	unsigned long pcSize=MAXNAMESIZE;
	if (GetComputerName(m_szLocalName,&pcSize))
	{
		if( pcSize >= MAXNAMESIZE)
		{
			m_szLocalName[MAXNAMESIZE-4]='.';
			m_szLocalName[MAXNAMESIZE-3]='.';
			m_szLocalName[MAXNAMESIZE-2]='.';
			m_szLocalName[MAXNAMESIZE-1]=0x00;
		}
	}

	//  Load the Rich Edit control DLL
	m_hRichEdit = LoadLibrary( "RICHED32.DLL" );
	if (!m_hRichEdit)
	{  
		yesUVNCMessageBox(m_hInstResDLL, NULL, sz_E1, sz_E2, MB_ICONEXCLAMATION );
		// Todo: do normal edit instead (no colors)
	}

}


//
//
//
TextChat::~TextChat()
{
	if (m_szLocalText != NULL) delete [] m_szLocalText;
	if (m_szLastLocalText != NULL) delete [] m_szLastLocalText;
	if (m_szRemoteText != NULL) delete [] m_szRemoteText;
	if (m_szRemoteName != NULL) delete [] m_szRemoteName;
	if (m_szLocalName != NULL) delete [] m_szLocalName;
	if (m_szTextBoxBuffer != NULL) delete [] m_szTextBoxBuffer;

	m_fTextChatRunning = false;
	SendMessage(m_hDlg, WM_COMMAND, IDOK, 0L);

	if (m_hRichEdit != NULL) FreeLibrary(m_hRichEdit);

}


//
// Set text format to a selection in the Chat area
//
void TextChat::SetTextFormat(bool bBold /*= false */, bool bItalic /*= false*/
	, long nSize /*= 0x75*/, const char* szFaceName /*= "MS Sans Serif"*/, DWORD dwColor /*= BLACK*/)
{
	if ( GetDlgItem( m_hDlg, IDC_CHATAREA_EDIT ) )  //  Sanity Check
	{		
		CHARFORMAT cf;
		memset( &cf, 0, sizeof(CHARFORMAT) ); //  Initialize structure

		cf.cbSize = sizeof(CHARFORMAT);             
		cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
		if (bBold)
		{
			cf.dwMask |= CFM_BOLD;
			cf.dwEffects |= CFE_BOLD;
		}
		if (bItalic)
		{
			cf.dwMask |= CFM_ITALIC;  
			cf.dwEffects |= CFE_ITALIC;
		}
		cf.crTextColor = dwColor;					// set color in AABBGGRR mode (alpha-RGB)
		cf.yHeight = nSize;							// set size in points
		strcpy_s( cf.szFaceName,32, szFaceName);
													
		SendDlgItemMessage( m_hDlg, IDC_CHATAREA_EDIT, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf );
	}
}


//
//
//
void TextChat::ProcessTextChatMsg() 
{
	rfbTextChatMsg tcm;
	m_pCC->ReadExact(((char *) &tcm) + m_pCC->m_nTO, sz_rfbTextChatMsg - m_pCC->m_nTO);
	UINT len = Swap32IfLE(tcm.length);
	
	if (len == CHAT_OPEN)
	{
		// Open Text Chat Dialog
		PostMessage(m_pCC->m_hwndMain, WM_SYSCOMMAND, ID_TEXTCHAT, (LPARAM)0L);
		return;
	}
	else if (len == CHAT_CLOSE)
	{
		// Close Text Chat Dialog
		if (!m_fTextChatRunning) return;
		PostMessage(m_hDlg, WM_COMMAND, IDOK, 0);
		return;
	}
	else if (len == CHAT_FINISHED)
	{
		// On Server notif, request FullScreen update
		if (!m_fTextChatRunning) return;
		m_fTextChatRunning = false;
		//adzm 2010-09 - all socket writes must remain on a single thread, but we only need an async request here
		m_pCC->SendAppropriateFramebufferUpdateRequest(true);
		return;
	}
	else
	{
		// Read the incoming text
		if (len > TEXTMAXSIZE) return; // Todo: Improve this...
		if (len == 0)
		{
			SetDlgItemText(m_hDlg, IDC_REMOTETEXT_EDIT, "");
			memset(m_szRemoteText, 0, TEXTMAXSIZE);
		}
		else
		{
			memset(m_szRemoteText, 0, TEXTMAXSIZE);
			m_pCC->ReadExact(m_szRemoteText, len);
			PrintMessage(m_szRemoteText, m_szRemoteName, RED);
			ShowChatWindow(true); // Show the chat window on new message reception
		}
	}
}


//
//
//
void TextChat::SendTextChatRequest(int nMsg)
{
    rfbTextChatMsg tcm;
	memset(&tcm, 0, sizeof(rfbTextChatMsg));
    tcm.type = rfbTextChat;
	tcm.length = Swap32IfLE(nMsg);
    m_pCC->WriteExact((char *)&tcm, sz_rfbTextChatMsg, rfbTextChat);
	return;
}

//
// Output messages in the chat area 
//
//
void TextChat::PrintMessage(const char* szMessage,const char* szSender,DWORD dwColor /*= BLACK*/)
{
	// char szTextBoxBuffer[TEXTMAXSIZE];			
	// memset(szTextBoxBuffer,0,TEXTMAXSIZE);			
	CHARRANGE cr;	
	
	GETTEXTLENGTHEX lpG;
	lpG.flags = GTL_NUMBYTES;
	lpG.codepage = CP_ACP; // ANSI

	int nLen = SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, EM_GETTEXTLENGTHEX, (WPARAM)&lpG, 0L);
	if (nLen + 32 > TEXTMAXSIZE )
	{
		memset(m_szTextBoxBuffer, 0, TEXTMAXSIZE);
		strcpy_s(m_szTextBoxBuffer, TEXTMAXSIZE, "------------------------------------------------------------------------------------------------------------------------\n");
		SetDlgItemText(m_hDlg, IDC_CHATAREA_EDIT, m_szTextBoxBuffer);
	}

	// Todo: test if chat text + sender + message > TEXTMAXSIZE -> Modulo display
	if (szSender != NULL) //print the sender's name
	{
		/*
		// Time
		char tbuffer[9];
		_tzset();
		_strtime(tbuffer);

		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,WM_GETTEXT, TEXTMAXSIZE-1,(LPARAM)m_szTextBoxBuffer);
		cr.cpMax = strlen(m_szTextBoxBuffer);	 // Select the last caracter to make the text insertion
		cr.cpMin  = cr.cpMax;
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,EM_EXSETSEL,0,(LPARAM) &cr);
		SetTextFormat(false, false, 0x75, "MS Sans Serif", GREY);
		_snprintf(m_szTextBoxBuffer, TEXTMAXSIZE-1, "[%s]", tbuffer);		
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,EM_REPLACESEL,FALSE,(LPARAM)m_szTextBoxBuffer); // Replace the selection with the message
		*/

		// Sender
		// SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,WM_GETTEXT, TEXTMAXSIZE-1,(LPARAM)m_szTextBoxBuffer);
		cr.cpMax = nLen; //strlen(m_szTextBoxBuffer);	 // Select the last caracter to make the text insertion
		cr.cpMin  = cr.cpMax;
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, EM_EXSETSEL, 0, (LPARAM)&cr);
		/***/

		// [v1.0.2-jp1 fix-->]
		//SetTextFormat(false, false, 0x75, "MS Sans Serif", dwColor);
		if(!m_hInstResDLL){
			SetTextFormat(false, false, 0x75, "MS Sans Serif", dwColor);
		}
		else{
			SetTextFormat(false, false, 0xb4, "ＭＳ ゴシック", dwColor);
		}
		// [<--v1.0.2-jp1 fix]

		_snprintf_s(m_szTextBoxBuffer, TEXTMAXSIZE -1, _TRUNCATE, "<%s>: ", szSender);
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,EM_REPLACESEL,FALSE,(LPARAM)m_szTextBoxBuffer); // Replace the selection with the message
	}

	nLen = SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, EM_GETTEXTLENGTHEX, (WPARAM)&lpG, 0L);
	if (szMessage != NULL) //print the sender's message
	{	
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,WM_GETTEXT, TEXTMAXSIZE-1,(LPARAM)m_szTextBoxBuffer);
		cr.cpMax = nLen; //strlen(m_szTextBoxBuffer);	 // Select the last caracter to make the text insertion
		cr.cpMin  = cr.cpMax;
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT,EM_EXSETSEL,0,(LPARAM) &cr);
		/***/
		// [v1.0.2-jp1 fix-->]
		//SetTextFormat(false, false, 0x75, "MS Sans Serif", dwColor != GREY ? BLACK : GREY);	
		if(!m_hInstResDLL){
			SetTextFormat(false, false, 0x75, "MS Sans Serif", dwColor != GREY ? BLACK : GREY);	
		}
		else{
			SetTextFormat(false, false, 0xb4, "ＭＳ ゴシック", dwColor != GREY ? BLACK : GREY);
		}
		// [<--v1.0.2-jp1 fix]

		_snprintf_s(m_szTextBoxBuffer, TEXTMAXSIZE-1, _TRUNCATE, "%s", szMessage);
		SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, EM_REPLACESEL, FALSE, (LPARAM)m_szTextBoxBuffer); 
	}

	// Scroll down the chat window
	// The following seems necessary under W9x & Me if we want the Edit to scroll to bottom...
	SCROLLINFO si;
    ZeroMemory(&si, sizeof(SCROLLINFO));
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE|SIF_PAGE;
    GetScrollInfo(GetDlgItem(m_hDlg, IDC_CHATAREA_EDIT), SB_VERT, &si);
	si.nPos = si.nMax - maximum(si.nPage - 1, 0);
	SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, WM_VSCROLL, MAKELONG(SB_THUMBPOSITION, si.nPos), 0L);	// Scroll down the ch

	// This line does the bottom scrolling correctly under Windows NT4, Windows 2000, Windows XP...
	// SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, WM_VSCROLL, SB_BOTTOM, 0L);

}

//
// Send local text content
//
void TextChat::SendLocalText(void)
{
	// We keep it because we could use it
	// for future retype functionality. (F3)
	memcpy(m_szLastLocalText, m_szLocalText, strlen(m_szLocalText));

	PrintMessage(m_szLocalText, m_szLocalName, BLUE);

    rfbTextChatMsg tcm;
	memset(&tcm, 0, sizeof(rfbTextChatMsg));
    tcm.type = rfbTextChat;
	tcm.length = Swap32IfLE(strlen(m_szLocalText));
	//adzm 2010-09
    m_pCC->WriteExactQueue((char *)&tcm, sz_rfbTextChatMsg, rfbTextChat);
	m_pCC->WriteExact((char *)m_szLocalText, strlen(m_szLocalText));

	//and we clear the input box
	SetDlgItemText(m_hDlg, IDC_INPUTAREA_EDIT, "");
	return;
}


//
//
//
int TextChat::DoDialog()
{
 	return DialogBoxParam(pApp->m_instance, DIALOG_MAKEINTRESOURCE(IDD_TEXTCHAT_DLG), 
							NULL, (DLGPROC) TextChatDlgProc, (LONG_PTR) this);
}


//
//
//
void TextChat::KillDialog()
{
	// DestroyWindow(m_hDlg);
    PostMessage(m_hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL,0), 0);
}

//
//
//
void TextChat::ShowChatWindow(bool fVisible)
{
	ShowWindow(m_hDlg, fVisible ? SW_RESTORE : SW_MINIMIZE);
	SetForegroundWindow(m_hDlg);
	// Put the Chat Windows always on Top if fullscreen
	if (fVisible && m_pCC->InFullScreenMode())
	{
		RECT Rect;
		GetWindowRect(m_hDlg, &Rect);
		SetWindowPos(m_hDlg, 
					HWND_TOPMOST,
					Rect.left,
					Rect.top,
					Rect.right - Rect.left,
					Rect.bottom - Rect.top,
					SWP_SHOWWINDOW);
	}
	//test m_fVisible always false
	//m_fVisible = fVisible; // This enables screen updates to be processed in ClientConnection
	// Refresh screen view if Chat window has been hidden
	//adzm 2010-09 - all socket writes must remain on a single thread, but we only need an async request here
	if (!fVisible)
		m_pCC->SendAppropriateFramebufferUpdateRequest(true);
}

// [v1.0.2-jp1 fix]
void AdjustLeft(LPRECT lprc)
{
	int cx = lprc->right - lprc->left - GetSystemMetrics(SM_CXSIZEFRAME) * 2;

	if(cx < 240){
		lprc->left = lprc->right - 240 - GetSystemMetrics(SM_CXSIZEFRAME) * 2;
	}
}

void AdjustTop(LPRECT lprc)
{
	int cy = lprc->bottom - lprc->top - GetSystemMetrics(SM_CYSIZEFRAME) * 2;

	if(cy < 179){
		lprc->top = lprc->bottom - 179 - GetSystemMetrics(SM_CYSIZEFRAME) * 2;
	}
}

void AdjustRight(LPRECT lprc)
{
	int cx = lprc->right - lprc->left - GetSystemMetrics(SM_CXSIZEFRAME) * 2;

	if(cx < 240){
		lprc->right = lprc->left + 240 + GetSystemMetrics(SM_CXSIZEFRAME) * 2;
	}
}

void AdjustBottom(LPRECT lprc)
{
	int cy = lprc->bottom - lprc->top - GetSystemMetrics(SM_CYSIZEFRAME) * 2;

	if(cy < 179){
		lprc->bottom = lprc->top + 179 + GetSystemMetrics(SM_CYSIZEFRAME) * 2;
	}
}

//
//
//
BOOL CALLBACK TextChat::TextChatDlgProc(  HWND hWnd,  UINT uMsg,  WPARAM wParam, LPARAM lParam )
{
    TextChat *_this = helper::SafeGetWindowUserData<TextChat>(hWnd);

	switch (uMsg)
	{

	case WM_INITDIALOG:
		{
			HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_TRAY));
			SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            helper::SafeSetWindowUserData(hWnd, lParam);
            TextChat *_this = (TextChat *) lParam;
			if (_this->m_szLocalText == NULL || _this->m_szRemoteText == NULL)
				EndDialog(hWnd, FALSE);

			// Window always on top if Fullscreen On
			if (_this->m_pCC->InFullScreenMode())
			{
				RECT Rect;
				GetWindowRect(hWnd, &Rect);
				SetWindowPos(hWnd, 
							HWND_TOPMOST,
							Rect.left,
							Rect.top,
							Rect.right - Rect.left,
							Rect.bottom - Rect.top,
							SWP_SHOWWINDOW);
			}

            // CentreWindow(hWnd);
			_this->m_hWnd = hWnd;
			_this->m_hDlg = hWnd;
			
			if (_snprintf_s(_this->m_szRemoteName, MAXNAMESIZE-1, _TRUNCATE, "%s", _this->m_pCC->m_desktopName) < 0 )
			{
				_this->m_szRemoteName[MAXNAMESIZE-4]='.';
				_this->m_szRemoteName[MAXNAMESIZE-3]='.';
				_this->m_szRemoteName[MAXNAMESIZE-2]='.';
				_this->m_szRemoteName[MAXNAMESIZE-1]=0x00;
			}	
			

			const long lTitleBufSize=256;			
			char szTitle[lTitleBufSize] = {};

			_snprintf_s(szTitle, lTitleBufSize-1, _TRUNCATE, "UltraVNC Viewer - Chat with <%s>",_this->m_szRemoteName);
			SetWindowText(hWnd, szTitle);			

			// Trunc the remote name for display in Chat Area before the first parenthesis, if any.
			char *p = strchr(_this->m_szRemoteName, '('); 
			if (p != NULL) *(p - 1) = '\0';

			//  Chat area			
			_this->SetTextFormat(); //  Set character formatting and background color
			SendDlgItemMessage( hWnd, IDC_CHATAREA_EDIT, EM_SETBKGNDCOLOR, FALSE, 0xFFFFFF ); 

			memset(_this->m_szLocalText, 0, TEXTMAXSIZE);
			// if (!_this->m_fPersistentTexts)
			{
				memset(_this->m_szLastLocalText, 0, TEXTMAXSIZE);
				memset(_this->m_szTextBoxBuffer, 0, TEXTMAXSIZE); // Clear Chat area 
				 //  Load and display diclaimer message
				// sf@2005 - Only if the DSMplugin is used
				if (_this->m_szRemoteText)
				if (!_this->m_pCC->m_fUsePlugin)
				{
					if (LoadString(_this->m_pApp->m_instance, IDS_WARNING, _this->m_szRemoteText, TEXTMAXSIZE -1) )
						_this->PrintMessage(_this->m_szRemoteText, NULL ,GREY);
				}
			}

			if (_this->m_szLocalText) SetDlgItemText(hWnd, IDC_INPUTAREA_EDIT, _this->m_szLocalText);
			if (_this->m_szTextBoxBuffer) SetDlgItemText(hWnd, IDC_CHATAREA_EDIT, _this->m_szTextBoxBuffer); // Chat area

			// Scroll down the chat window
			// The following seems necessary under W9x & Me if we want the Edit to scroll to bottom...
			SCROLLINFO si;
			ZeroMemory(&si, sizeof(SCROLLINFO));
			si.cbSize = sizeof(SCROLLINFO);
			si.fMask = SIF_RANGE|SIF_PAGE;
			GetScrollInfo(GetDlgItem(hWnd, IDC_CHATAREA_EDIT), SB_VERT, &si);
			si.nPos = si.nMax - maximum(si.nPage - 1, 0);
			SendDlgItemMessage(hWnd, IDC_CHATAREA_EDIT, WM_VSCROLL, MAKELONG(SB_THUMBPOSITION, si.nPos), 0L);	
			// This line does the bottom scrolling correctly under Windows NT4, Windows 2000, Windows XP...
			// SendDlgItemMessage(m_hDlg, IDC_CHATAREA_EDIT, WM_VSCROLL, SB_BOTTOM, 0L);

			// SendDlgItemMessage(hWnd, IDC_PERSISTENT_CHECK, BM_SETCHECK, _this->m_fPersistentTexts, 0);

			// Tell the other side to open the Text Chat Window
			_this->SendTextChatRequest(CHAT_OPEN);

			SetForegroundWindow(hWnd);
			
			// [v1.0.2-jp1 fix] SUBCLASS Split bar
            pDefSBProc = helper::SafeGetWindowProc(GetDlgItem(hWnd, IDC_STATIC_SPLIT));
            helper::SafeSetWindowProc(GetDlgItem(hWnd, IDC_STATIC_SPLIT), (LONG_PTR)SBProc);

			return TRUE;
		}
		// break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		/*
		case IDC_PERSISTENT_CHECK:
		    _this->m_fPersistentTexts = (SendDlgItemMessage(hWnd, IDC_PERSISTENT_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
			return TRUE;
		*/

		case IDOK:
			// Server orders to close Text Chat 			

			// [v1.0.2-jp1 fix] UNSUBCLASS Split bar
            helper::SafeSetWindowProc(GetDlgItem(hWnd, IDC_STATIC_SPLIT), pDefSBProc);

			EndDialog(hWnd, FALSE);
			return TRUE;

		case IDCANCEL:			
			_this->SendTextChatRequest(CHAT_CLOSE); // Server must close Text Chat

			// [v1.0.2-jp1 fix] UNSUBCLASS Split bar
            helper::SafeSetWindowProc(GetDlgItem(hWnd, IDC_STATIC_SPLIT), pDefSBProc);

			EndDialog(hWnd, FALSE);
			return TRUE;

		case IDC_SEND_B:
			{
			memset(_this->m_szLocalText,0,TEXTMAXSIZE);
			UINT nRes = GetDlgItemText( hWnd, IDC_INPUTAREA_EDIT, _this->m_szLocalText, TEXTMAXSIZE-1);
			strcat_s(_this->m_szLocalText, TEXTMAXSIZE, "\n");
			_this->SendLocalText();		
			SetFocus(GetDlgItem(hWnd, IDC_INPUTAREA_EDIT));
			}
			return TRUE;

		case IDC_HIDE_B:
			_this->ShowChatWindow(false);
			return TRUE;

		case IDC_INPUTAREA_EDIT:
			if(HIWORD(wParam) == EN_UPDATE)			
			{
				memset(_this->m_szLocalText,0,TEXTMAXSIZE);
				UINT nRes = GetDlgItemText( hWnd, IDC_INPUTAREA_EDIT, _this->m_szLocalText, TEXTMAXSIZE);
				if (strstr(_this->m_szLocalText, "\n") != 0 )
				{
					_this->SendLocalText();			
				}								
			}
			return TRUE;
		}
		break;

	case WM_SYSCOMMAND:
		switch (LOWORD(wParam))
		{
		case SC_RESTORE:
			_this->ShowChatWindow(true);
			//SetFocus(GetDlgItem(hWnd, IDC_INPUTAREA_EDIT));
			return TRUE;
		}
		break;

	// [v1.0.2-jp1 fix-->]
	case WM_SIZING:
		LPRECT lprc;
		lprc = (LPRECT)lParam;
		switch(wParam){
		case WMSZ_TOPLEFT:
			AdjustTop(lprc);
			AdjustLeft(lprc);
		case WMSZ_TOP:
			AdjustTop(lprc);
		case WMSZ_TOPRIGHT:
			AdjustTop(lprc);
			AdjustRight(lprc);
		case WMSZ_LEFT:
			AdjustLeft(lprc);
		case WMSZ_RIGHT:
			AdjustRight(lprc);
		case WMSZ_BOTTOMLEFT:
			AdjustBottom(lprc);
			AdjustLeft(lprc);
		case WMSZ_BOTTOM:
			AdjustBottom(lprc);
		case WMSZ_BOTTOMRIGHT:
			AdjustBottom(lprc);
			AdjustRight(lprc);
		}
		return TRUE;
	case WM_SIZE:
		int cx;
		int cy;
		int icy;
		RECT rc;

		if(wParam == SIZE_MINIMIZED){
			break;
		}

		cx = LOWORD(lParam);
		cy = HIWORD(lParam);
		GetWindowRect(GetDlgItem(hWnd, IDC_INPUTAREA_EDIT), &rc);
		icy = rc.bottom - rc.top;
		if(cy - icy - 12 < 80){
			icy = cy - 92;
		}
		MoveWindow(GetDlgItem(hWnd, IDC_CHATAREA_EDIT),  4,             4, cx -  8, cy - icy - 16, TRUE); 
		MoveWindow(GetDlgItem(hWnd, IDC_STATIC_SPLIT),   4, cy - icy - 12, cx -  8,             8, TRUE); 
		MoveWindow(GetDlgItem(hWnd, IDC_INPUTAREA_EDIT), 4, cy - icy -  4, cx - 88,           icy, TRUE);

		MoveWindow(GetDlgItem(hWnd, IDC_SEND_B), cx - 76, cy - 64, 72, 20, TRUE); 
		MoveWindow(GetDlgItem(hWnd, IDC_HIDE_B), cx - 76, cy - 40, 72, 18, TRUE); 
		MoveWindow(GetDlgItem(hWnd, IDCANCEL),   cx - 76, cy - 22, 72, 18, TRUE);

		InvalidateRect(hWnd, NULL, FALSE);
		return TRUE;
	// [<--v1.0.2-jp1 fix]

	case WM_DESTROY:
		// _this->SendTextChatRequest(_this, CHAT_FINISHED);
		EndDialog(hWnd, FALSE);
		_this->m_fTextChatRunning = false;
		return TRUE;
	}
	return 0;
}

// [JEE6 Fix] Split bar
void DrawResizeLine(HWND hWnd, int y)
{
	HWND hParent;
	RECT rc;
	HDC hDC;

	hParent = GetParent(hWnd);
	GetClientRect(hParent, &rc);

	if(y < 80){
		y = 80;
	}
	else if(y > rc.bottom - 80){
		y = rc.bottom - 80;
	}
	
	hDC = GetDC(hParent);
	SetROP2(hDC, R2_NOTXORPEN);
	MoveToEx(hDC, rc.left, y, NULL);
	LineTo(hDC, rc.right, y);
	MoveToEx(hDC, rc.left, y+1, NULL);
	LineTo(hDC, rc.right, y+1);
	MoveToEx(hDC, rc.left, y+2, NULL);
	LineTo(hDC, rc.right, y+2);
	MoveToEx(hDC, rc.left, y+3, NULL);
	LineTo(hDC, rc.right, y+3);
	ReleaseDC(hParent, hDC);
}

// [v1.0.2-jp1 fix] Split bar proc
LRESULT CALLBACK SBProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static BOOL bCapture;
	static UINT u;
	static int oldy;
	HWND hParent;
	RECT rc;
	POINT cp;
	int y;
	int cx;
	int cy;

	switch(uMsg){
	case WM_SETCURSOR:
		SetCursor(LoadCursor(NULL, IDC_SIZENS));
		return TRUE;
	case WM_LBUTTONDOWN:
		SetCapture(hWnd);
		bCapture = TRUE;
		u = GetCaretBlinkTime();
		SetCaretBlinkTime(0x7fffffff);
		GetCursorPos(&cp);
		hParent = GetParent(hWnd);
		ScreenToClient(hParent, &cp);
		DrawResizeLine(hWnd, cp.y);
		oldy = cp.y;
		break;
	case WM_MOUSEMOVE:
		if(bCapture){
			GetCursorPos(&cp);
			hParent = GetParent(hWnd);
			ScreenToClient(hParent, &cp);
			DrawResizeLine(hWnd, oldy);
			DrawResizeLine(hWnd, cp.y);
			oldy = cp.y;
		}
		break;
	case WM_LBUTTONUP:
		ReleaseCapture();
		bCapture = FALSE;
		SetCaretBlinkTime(u);
		GetCursorPos(&cp);
		hParent = GetParent(hWnd);
		GetClientRect(hParent, &rc);
		cx = rc.right - rc.left;
		cy = rc.bottom - rc.top;
		ScreenToClient(hParent, &cp);
		DrawResizeLine(hWnd, cp.y);
		y = cp.y;
		if(y < 80){
			y = 80;
		}
		else if(y > cy - 80){
			y = cy - 80;
		}
		MoveWindow(GetDlgItem(hParent, IDC_CHATAREA_EDIT),  4,         4, cx -  8,       y - 4, TRUE); 
		MoveWindow(GetDlgItem(hParent, IDC_STATIC_SPLIT),   4,         y, cx -  8,           8, TRUE); 
		MoveWindow(GetDlgItem(hParent, IDC_INPUTAREA_EDIT), 4,     y + 8, cx - 88, cy - y - 12, TRUE);
		break;
	}

	return CallWindowProc((WNDPROC)pDefSBProc, hWnd, uMsg, wParam, lParam);
}
