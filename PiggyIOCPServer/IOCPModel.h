
/*
==========================================================================

Purpose:

	* �����CIOCPModel�Ǳ�����ĺ����࣬����˵��WinSock�������˱��ģ���е�
	  ��ɶ˿�(IOCP)��ʹ�÷�������ʹ��MFC�Ի�����������������ʵ���˻�����
	  ����������ͨ�ŵĹ��ܡ�

	* ���е�PER_IO_DATA�ṹ���Ƿ�װ������ÿһ���ص������Ĳ���
	  PER_HANDLE_DATA �Ƿ�װ������ÿһ��Socket�Ĳ�����Ҳ��������ÿһ����ɶ˿ڵĲ���

	* ��ϸ���ĵ�˵����ο� http://blog.csdn.net/PiggyXP

Notes:

	* ���彲���˷������˽�����ɶ˿ڡ������������̡߳�Ͷ��Recv����Ͷ��Accept����ķ�����
	  ���еĿͻ��������Socket����Ҫ�󶨵�IOCP�ϣ����дӿͻ��˷��������ݣ�����ʵʱ��ʾ��
	  ��������ȥ��


==========================================================================
*/

#pragma once

// winsock 2 ��ͷ�ļ��Ϳ�
#include <winsock2.h>
#include <MSWSock.h>
#pragma comment(lib,"ws2_32.lib")

#include <string>
#include <map>
#include <list>
using namespace std;

#include "StreamLog.h"
#include "lock.h"
#include "agsMSMQ.h"

// ���������� (1024*8)
// ֮����Ϊʲô����8K��Ҳ��һ�������ϵľ���ֵ
// ���ȷʵ�ͻ��˷�����ÿ�����ݶ��Ƚ��٣���ô�����õ�СһЩ��ʡ�ڴ�
#define MAX_BUFFER_LEN        1024*8  
// Ĭ�϶˿�
#define DEFAULT_PORT          12345    
// Ĭ��IP��ַ
#define DEFAULT_IP            _T("127.0.0.1")
#define DEFAULT_ASQL_THREAD_COUNT		1

//�����û���copy�Ļ�������С
#define MAXQUEUE_NUM			  1000000
#define MAXQUEUE_BUFFER_LEN       1024*8

//////////////////////////////////////////////////////////////////
// ����ɶ˿���Ͷ�ݵ�I/O����������
typedef enum _OPERATION_TYPE  
{  
	ACCEPT_POSTED,                     // ��־Ͷ�ݵ�Accept����
	SEND_POSTED,                       // ��־Ͷ�ݵ��Ƿ��Ͳ���
	RECV_POSTED,                       // ��־Ͷ�ݵ��ǽ��ղ���
	NULL_POSTED                        // ���ڳ�ʼ����������

}OPERATION_TYPE;

//====================================================================================
//
//				��IO���ݽṹ�嶨��(����ÿһ���ص������Ĳ���)
//
//====================================================================================

typedef struct _PER_IO_CONTEXT
{
	OVERLAPPED     m_Overlapped;                               // ÿһ���ص�����������ص��ṹ(���ÿһ��Socket��ÿһ����������Ҫ��һ��)              
	SOCKET         m_sockAccept;                               // ������������ʹ�õ�Socket
	WSABUF         m_wsaBuf;                                   // WSA���͵Ļ����������ڸ��ص�������������
	
	OPERATION_TYPE m_OpType;                                   // ��ʶ�������������(��Ӧ�����ö��)

	DWORD			m_nTotalBytes;								//�����ܵ��ֽ���
	DWORD			m_nSendBytes;								//�Ѿ����͵��ֽ�������δ��������������Ϊ0
	DWORD           m_dwTick;                                   //����ʱ��ͬ��

	char           m_szBuffer[MAX_BUFFER_LEN];                 // �����WSABUF�������ַ��Ļ�����
	unsigned char           m_szMqBuf[2048];                            // ��������ڷ���Ϣ����Ϣ������
	// ��ʼ��
	_PER_IO_CONTEXT()
	{
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));  
		ZeroMemory( m_szBuffer,MAX_BUFFER_LEN );
		ZeroMemory( m_szMqBuf,2048 );
		m_sockAccept = INVALID_SOCKET;
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = MAX_BUFFER_LEN;
		m_OpType     = NULL_POSTED;

		m_nTotalBytes	= 0;
		m_nSendBytes	= 0;
	}
	// �ͷŵ�Socket
	~_PER_IO_CONTEXT()
	{
		if( m_sockAccept!=INVALID_SOCKET )
		{
			closesocket(m_sockAccept);
			m_sockAccept = INVALID_SOCKET;
		}
	}
	// ���û���������
	void ResetBuffer()
	{
		ZeroMemory( m_szBuffer,MAX_BUFFER_LEN );
	}

} PER_IO_CONTEXT, *PPER_IO_CONTEXT;

// typedef struct _PER_IO_OPERATION_DATA
// {
// 	OVERLAPPED Overlapped;
// 	WSABUF DataBuff[1];
// 	char Buff[24];
// 	OPERATION_TYPE OperationType;
// }PER_IO_OPERATION_DATA,* LPPER_IO_OPERATION_DATA;



//====================================================================================
//
//				��������ݽṹ�嶨��(����ÿһ����ɶ˿ڣ�Ҳ����ÿһ��Socket�Ĳ���)
//
//====================================================================================

typedef struct _PER_SOCKET_CONTEXT
{  
	SOCKET      m_Socket;                                  // ÿһ���ͻ������ӵ�Socket
	SOCKADDR_IN m_ClientAddr;                              // �ͻ��˵ĵ�ַ
	CArray<_PER_IO_CONTEXT*> m_arrayIoContext;             // �ͻ���������������������ݣ�
	                                                       // Ҳ����˵����ÿһ���ͻ���Socket���ǿ���������ͬʱͶ�ݶ��IO�����

	// ��ʼ��
	_PER_SOCKET_CONTEXT()
	{
		m_Socket = INVALID_SOCKET;
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr)); 
	}

	// �ͷ���Դ
	~_PER_SOCKET_CONTEXT()
	{
		if( m_Socket!=INVALID_SOCKET )
		{
			closesocket( m_Socket );
		    m_Socket = INVALID_SOCKET;
		}
		// �ͷŵ����е�IO����������
		for( int i=0;i<m_arrayIoContext.GetCount();i++ )
		{
			delete m_arrayIoContext.GetAt(i);
		}
		m_arrayIoContext.RemoveAll();
	}

	// ��ȡһ���µ�IoContext
	_PER_IO_CONTEXT* GetNewIoContext()
	{
		_PER_IO_CONTEXT* p = new _PER_IO_CONTEXT;

		m_arrayIoContext.Add( p );

		return p;
	}

	// ���������Ƴ�һ��ָ����IoContext
	void RemoveContext( _PER_IO_CONTEXT* pContext )
	{
		ASSERT( pContext!=NULL );

		for( int i=0;i<m_arrayIoContext.GetCount();i++ )
		{
			if( pContext==m_arrayIoContext.GetAt(i) )
			{
				delete pContext;
				pContext = NULL;
				m_arrayIoContext.RemoveAt(i);				
				break;
			}
		}
	}

} PER_SOCKET_CONTEXT, *PPER_SOCKET_CONTEXT;




//====================================================================================
//
//				CIOCPModel�ඨ��
//
//====================================================================================

// �������̵߳��̲߳���
class CIOCPModel;
typedef struct _tagThreadParams_WORKER
{
	CIOCPModel* pIOCPModel;                                   // ��ָ�룬���ڵ������еĺ���
	int         nThreadNo;                                    // �̱߳��

} THREADPARAMS_WORKER,*PTHREADPARAM_WORKER; 

#define PKT_PARA_MAX_SIZE		255
// ��Ӧ��
typedef struct      
{
	unsigned char bSOF;		// == 0x0B
	unsigned char bReaderAddr[4];
	unsigned char bPktLen;
	unsigned char bStatus;
	unsigned char aPara[PKT_PARA_MAX_SIZE];// Clude CheckSum
}Comm_Pkt_Format_t; 


// ���ݲ���
typedef struct
{
	unsigned char bTagType;
	unsigned char aTagId[4];
	unsigned char bCheckSum;
	unsigned char aRsv[4];
	unsigned char bcdTime[6];
}Record_t;


typedef struct 
{
	unsigned char bCnt;
	Record_t  mRecord[50];
}Record_list;

// ��������ṹ��
//typedef struct Post_Info
//{
//	string s_version;
//	string s_action;
//	string s_timeStamp;
//	string s_signed;
//	string s_cbid;//�豸ID
//	string s_requrl;//����url
//	string s_body;//tagData
//}Post_Info, *pPost_Info;

//����Э��
typedef struct _tagReportData 
{
	string s_appKey;			//���û���ƽ̨���룬ƽ̨�Զ�����Ψһ��appKey
	string s_method;			//�ӿڷ�������
	string s_ver;				//����ӿڵİ汾��
	string s_messageFormat;		//�ӿ���������json/xml
	string s_cbid;				//�豸id
	string s_dataSource;		//������Դ���п�/�첨��ZK/TB
	string s_device_type;		//1.����У�豸��2.�����豸
	string s_sign;
	string s_body;
}tagReportData,*pReportData;

typedef struct _tagTime
{
	unsigned long nTime_1;
	unsigned long nTime_2;
	_tagTime()
	{
		nTime_1 = 0;
		nTime_2 = 0;
	}
}tagTime;

typedef map<unsigned int,tagTime> mapringID2Time;

typedef struct _tag_reader{
	unsigned int readerid;				//��ǰ��ǩ���ڵ��Ķ���ID
	unsigned int last_readerid;			//�ϴα��������Ķ���
	string s_firttime;					//��һ�α�������ʱ��
	string s_lasttime;					//���һ�ζ�����ʱ��
	DWORD dwCurTime;					//��ǰ������ʱ��
	DWORD dwLastread;					//
	_tag_reader()
	{
		last_readerid	= 0;
		readerid		= 0;
		dwCurTime		= 0;
		dwLastread		= 0;
	}
}tag_reader;

typedef map<unsigned long,tag_reader> mapfilter_time;

typedef struct _tag_heart{
	//SOCKADDR_IN* socketaddr;             //��ǰ����
    string s_ip;					    //��ǰ����ip��ַ
	DWORD dwCurTime;					//��ǰ������ʱ��
	DWORD dwLastread;					//���ݿ���µ�ʱ��
	_tag_heart()
	{
		//socketaddr      = 0;
		dwCurTime		= 0;
		dwLastread		= 0;
		s_ip = "";
	}
}tag_heart;
typedef map<unsigned int,tag_heart> mapfilter_heart;
// CIOCPModel��
class CIOCPModel
{
public:
	CIOCPModel(void);
	~CIOCPModel(void);

public:

	// ����������
	bool Start();

	//	ֹͣ������
	void Stop();

	// ����Socket��
	bool LoadSocketLib();

	// ж��Socket�⣬��������
	void UnloadSocketLib() { WSACleanup(); }

	// ��ñ�����IP��ַ
	CString GetLocalIP();

	// ���ü����˿�
	void SetPort( const int& nPort ) { m_nPort=nPort; }

	// �����������ָ�룬���ڵ�����ʾ��Ϣ��������
	void SetMainDlg( CDialog* p ) { m_pMain=p; }

	public:

	// ��ʼ��IOCP
	bool _InitializeIOCP();

	// ��ʼ��Socket
	bool _InitializeListenSocket();

	// ����ͷ���Դ
	void _DeInitialize();

	// Ͷ��Accept����
	bool _PostAccept( PER_IO_CONTEXT* pAcceptIoContext ); 

	// Ͷ�ݽ�����������
	bool _PostRecv( PER_IO_CONTEXT* pIoContext );

	// ���пͻ��������ʱ�򣬽��д���
	bool _DoAccpet( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );

	// ��������
	bool _DoSend( PER_IO_CONTEXT* pSendIoContext );

	// ���н��յ����ݵ����ʱ�򣬽��д���
	bool _DoRecv( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );

	// ���ͻ��˵������Ϣ�洢��������
	void _AddToContextList( PER_SOCKET_CONTEXT *pSocketContext );

	// ���ͻ��˵���Ϣ���������Ƴ�
	void _RemoveContext( PER_SOCKET_CONTEXT *pSocketContext );

	// ��տͻ�����Ϣ
	void _ClearContextList();

	// ������󶨵���ɶ˿���
	bool _AssociateWithIOCP( PER_SOCKET_CONTEXT *pContext);

	// ������ɶ˿��ϵĴ���
	bool HandleError( PER_SOCKET_CONTEXT *pContext,const DWORD& dwErr );

	// �̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);

	// ��ñ����Ĵ���������
	int _GetNoOfProcessors();

	// �жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool _IsSocketAlive(SOCKET s);

	// ������������ʾ��Ϣ
	void _ShowMessage( const CString szFormat,...) const;

	// ��������
	int _GetRecordList(unsigned char *pRspPkt, unsigned char *bReaderAddr,unsigned char *bReaderType, Record_list *pRecordList);

	// У���
	BYTE _Check_Sum(const BYTE * buf, int len);

	//��ȡ����ʱ��
	void _GetCurrenttime(char *currtime);

	//ʶ����Ϣת��json��ʽ
	void _RecognitionToJson(unsigned char *reader, unsigned char *wristband, unsigned char *time, char *JsonString, unsigned char *type);

	//ʶ����Ϣת��json��ʽ
	void _RecognitionToJson(int scan_rssi, unsigned char *wristband, unsigned char *time, char *JsonString, unsigned char *type);

	//��ȡ������ʱ��ת��json��ʽ
	void _GetSeverTimeTOJson(int devicetype, unsigned char *device, unsigned char *time, char *JsonString);

	//����Э�齫����תΪ�ַ���
	void _HttpPostStr(unsigned int ireaderID,Record_t* record,unsigned char *type,unsigned char *inouttype,unsigned char *outbuf = NULL);

	//�ַ������Ƴ��ַ�
	void removeAll(string &str,char ch);

	//asciiתhex
	void Ascii2Hex(unsigned char *hexBuf, const char *ascBuf, unsigned char hexbufLen);

	//ʱ��ȽϺ���
	bool _CompareTime();

	//ʱ��ȽϺ���
	bool GetCurHourByMin(time_t curTm, int BegHour, int BegMin, int EndHour, int EndMin);

	//
	string ToHex(unsigned long iNum);
	//BCD
	int  DectoBCD(int Dec, unsigned char *Bcd, int len) ;

private:

	HANDLE                       m_hShutdownEvent;              // ����֪ͨ�߳�ϵͳ�˳����¼���Ϊ���ܹ����õ��˳��߳�

	HANDLE                       m_hIOCompletionPort;           // ��ɶ˿ڵľ��

	HANDLE*                      m_phWorkerThreads;             // �������̵߳ľ��ָ��

	int		                     m_nThreads;                    // ���ɵ��߳�����

	CString                      m_strIP;                       // �������˵�IP��ַ


	CDialog*                     m_pMain;                       // ������Ľ���ָ�룬����������������ʾ��Ϣ

	CRITICAL_SECTION             m_csContextList;               // ����Worker�߳�ͬ���Ļ�����

	CArray<PER_SOCKET_CONTEXT*>  m_arrayClientContext;          // �ͻ���Socket��Context��Ϣ        

	PER_SOCKET_CONTEXT*          m_pListenContext;              // ���ڼ�����Socket��Context��Ϣ

	LPFN_ACCEPTEX                m_lpfnAcceptEx;                // AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
	LPFN_GETACCEPTEXSOCKADDRS    m_lpfnGetAcceptExSockAddrs; 

public:
	int                          m_nPort;                       // �������˵ļ����˿�
	string DataIP;
	string Catalog;
	string UserID;
	string Password;
	map<string, string> TimeMap;
	string URL;
	string AppKey;
	//_variant_t vAffected;
	//_bstr_t sql;

	//_ConnectionPtr pMyConnect;
	//_RecordsetPtr m_pRecordset;//��¼������ָ�룬����ִ��SQL��䲢��¼��ѯ���	
	//_RecordsetPtr m_pNoteset;

	string str_sesssion;
	string str_username;
	string str_passwd;

	//add by ljj
	int autorun;
	int wlogflag;

public:
	void _Gettimerecord();
	
	void _InitSQL();

	void _SaveConfig();

	// ������
	time_t StringToDatetime(char *str);

	//��ȡ����ʱ��
	void _SQLformatTime(char *currtime);
	// �̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI _ListenThread(LPVOID lpParam);
    // �����̺߳�����ΪIOCP��������
	static DWORD WINAPI _ProcessThread(LPVOID lpParam);
     //�����߳�
	void _Process_data();
	//�����̣߳�ֻ����һ������)
	void _Process_data(unsigned char *iocpbuf,unsigned char* mqbuf);
	//������Ϣ����
	void _SendMsmq(CString labelmsg,CString msg);
	//������Ϣ����
	void _SendMsmq(CString labelmsg,CString msg,unsigned char* msgbuf);

private:
	CRITICAL_SECTION             m_csSqlList;               // ����sqlserver���ʵĻ�����
	HANDLE						 hThread_heart;
	DEFTOOLS::LogFile a;
	
    std::list<unsigned char*> processList;           //�����̶߳���
	bool                  g_bExtiThread;             //ȫ���˳�����
	CRITICAL_SECTION             m_csProcess;        //�����߳���
	//HANDLE						 hThread_process;//�����߳̾��
	HANDLE hThread[DEFAULT_ASQL_THREAD_COUNT];
	agsMSMQ mque; //��Ϣ����
};

