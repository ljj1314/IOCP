// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�

#pragma once

#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // �� Windows ͷ���ų�����ʹ�õ�����
#endif

#include "targetver.h"

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // ĳЩ CString ���캯��������ʽ��

// �ر� MFC ��ĳЩ�����������ɷ��ĺ��Եľ�����Ϣ������
#define _AFX_ALL_WARNINGS

#include <afxwin.h>         // MFC ��������ͱ�׼���
#include <afxext.h>         // MFC ��չ

// ��ǩ�ֽ���
#define TAG_ID_BYTES_CNT				4

//#define  KEY  _T("bf9f8d1f05dc08cc3b02e8fcf2c2ba57")

#ifndef _AFX_NO_OLE_SUPPORT
#include <afxdtctl.h>           // MFC �� Internet Explorer 4 �����ؼ���֧��
#endif
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>                     // MFC �� Windows �����ؼ���֧��
#endif // _AFX_NO_AFXCMN_SUPPORT


//#include "include/json/json.h"
//#pragma comment(lib,"json_vc71_libmtd.lib")
#include <algorithm>

//#define  KEY    _T("bf9f8d1f05dc08cc3b02e8fcf2c2ba57")
#define  KEY    _T("8adaed4c-f662-4d16-9054-3bad2b1516be")

//#include <afxdao.h>         //����DAO���ݿ�֧��
// ADO ֧��
//#import "c:\program files\common files\system\ado\msado15.dll" no_namespace rename("EOF", "EndOfFile")
#include <iostream>
#include <stdio.h>
#include <ICRSINT.H>
#include <string>
using namespace std;
//#import "c:\program files\common files\system\ado\msado15.dll"  no_namespace rename("EOF", "adoEOF")




typedef struct itermstruct
{
	unsigned char rID;
	char rtimr[20];
}itemS;


#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

#include "Resource.h"
