
/*
==========================================================================

Purpose:

	* 这个类CIOCPModel是本代码的核心类，用于说明WinSock服务器端编程模型中的
	  完成端口(IOCP)的使用方法，并使用MFC对话框程序来调用这个类实现了基本的
	  服务器网络通信的功能。

	* 其中的PER_IO_DATA结构体是封装了用于每一个重叠操作的参数
	  PER_HANDLE_DATA 是封装了用于每一个Socket的参数，也就是用于每一个完成端口的参数

	* 详细的文档说明请参考 http://blog.csdn.net/PiggyXP

Notes:

	* 具体讲明了服务器端建立完成端口、建立工作者线程、投递Recv请求、投递Accept请求的方法，
	  所有的客户端连入的Socket都需要绑定到IOCP上，所有从客户端发来的数据，都会实时显示到
	  主界面中去。


==========================================================================
*/

#pragma once

// winsock 2 的头文件和库
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

// 缓冲区长度 (1024*8)
// 之所以为什么设置8K，也是一个江湖上的经验值
// 如果确实客户端发来的每组数据都比较少，那么就设置得小一些，省内存
#define MAX_BUFFER_LEN        1024*8  
// 默认端口
#define DEFAULT_PORT          12345    
// 默认IP地址
#define DEFAULT_IP            _T("127.0.0.1")
#define DEFAULT_ASQL_THREAD_COUNT		1

//这是用户端copy的缓冲区大小
#define MAXQUEUE_NUM			  1000000
#define MAXQUEUE_BUFFER_LEN       1024*8

//////////////////////////////////////////////////////////////////
// 在完成端口上投递的I/O操作的类型
typedef enum _OPERATION_TYPE  
{  
	ACCEPT_POSTED,                     // 标志投递的Accept操作
	SEND_POSTED,                       // 标志投递的是发送操作
	RECV_POSTED,                       // 标志投递的是接收操作
	NULL_POSTED                        // 用于初始化，无意义

}OPERATION_TYPE;

//====================================================================================
//
//				单IO数据结构体定义(用于每一个重叠操作的参数)
//
//====================================================================================

typedef struct _PER_IO_CONTEXT
{
	OVERLAPPED     m_Overlapped;                               // 每一个重叠网络操作的重叠结构(针对每一个Socket的每一个操作，都要有一个)              
	SOCKET         m_sockAccept;                               // 这个网络操作所使用的Socket
	WSABUF         m_wsaBuf;                                   // WSA类型的缓冲区，用于给重叠操作传参数的
	
	OPERATION_TYPE m_OpType;                                   // 标识网络操作的类型(对应上面的枚举)

	DWORD			m_nTotalBytes;								//数据总的字节数
	DWORD			m_nSendBytes;								//已经发送的字节数，如未发送数据则设置为0
	DWORD           m_dwTick;                                   //控制时间同步

	char           m_szBuffer[MAX_BUFFER_LEN];                 // 这个是WSABUF里具体存字符的缓冲区
	unsigned char           m_szMqBuf[2048];                            // 这个是用于发消息给消息队列用
	// 初始化
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
	// 释放掉Socket
	~_PER_IO_CONTEXT()
	{
		if( m_sockAccept!=INVALID_SOCKET )
		{
			closesocket(m_sockAccept);
			m_sockAccept = INVALID_SOCKET;
		}
	}
	// 重置缓冲区内容
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
//				单句柄数据结构体定义(用于每一个完成端口，也就是每一个Socket的参数)
//
//====================================================================================

typedef struct _PER_SOCKET_CONTEXT
{  
	SOCKET      m_Socket;                                  // 每一个客户端连接的Socket
	SOCKADDR_IN m_ClientAddr;                              // 客户端的地址
	CArray<_PER_IO_CONTEXT*> m_arrayIoContext;             // 客户端网络操作的上下文数据，
	                                                       // 也就是说对于每一个客户端Socket，是可以在上面同时投递多个IO请求的

	// 初始化
	_PER_SOCKET_CONTEXT()
	{
		m_Socket = INVALID_SOCKET;
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr)); 
	}

	// 释放资源
	~_PER_SOCKET_CONTEXT()
	{
		if( m_Socket!=INVALID_SOCKET )
		{
			closesocket( m_Socket );
		    m_Socket = INVALID_SOCKET;
		}
		// 释放掉所有的IO上下文数据
		for( int i=0;i<m_arrayIoContext.GetCount();i++ )
		{
			delete m_arrayIoContext.GetAt(i);
		}
		m_arrayIoContext.RemoveAll();
	}

	// 获取一个新的IoContext
	_PER_IO_CONTEXT* GetNewIoContext()
	{
		_PER_IO_CONTEXT* p = new _PER_IO_CONTEXT;

		m_arrayIoContext.Add( p );

		return p;
	}

	// 从数组中移除一个指定的IoContext
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
//				CIOCPModel类定义
//
//====================================================================================

// 工作者线程的线程参数
class CIOCPModel;
typedef struct _tagThreadParams_WORKER
{
	CIOCPModel* pIOCPModel;                                   // 类指针，用于调用类中的函数
	int         nThreadNo;                                    // 线程编号

} THREADPARAMS_WORKER,*PTHREADPARAM_WORKER; 

#define PKT_PARA_MAX_SIZE		255
// 响应包
typedef struct      
{
	unsigned char bSOF;		// == 0x0B
	unsigned char bReaderAddr[4];
	unsigned char bPktLen;
	unsigned char bStatus;
	unsigned char aPara[PKT_PARA_MAX_SIZE];// Clude CheckSum
}Comm_Pkt_Format_t; 


// 数据参数
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

// 输入参数结构体
//typedef struct Post_Info
//{
//	string s_version;
//	string s_action;
//	string s_timeStamp;
//	string s_signed;
//	string s_cbid;//设备ID
//	string s_requrl;//请求url
//	string s_body;//tagData
//}Post_Info, *pPost_Info;

//天喻协议
typedef struct _tagReportData 
{
	string s_appKey;			//由用户向平台申请，平台自动产生唯一的appKey
	string s_method;			//接口服务名称
	string s_ver;				//服务接口的版本号
	string s_messageFormat;		//接口数据类型json/xml
	string s_cbid;				//设备id
	string s_dataSource;		//数据来源（中科/天波）ZK/TB
	string s_device_type;		//1.进出校设备，2.进班设备
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
	unsigned int readerid;				//当前标签所在的阅读器ID
	unsigned int last_readerid;			//上次被读到的阅读器
	string s_firttime;					//第一次被读到的时间
	string s_lasttime;					//最后一次读到的时间
	DWORD dwCurTime;					//当前读到的时间
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
	//SOCKADDR_IN* socketaddr;             //当前连接
    string s_ip;					    //当前连接ip地址
	DWORD dwCurTime;					//当前心跳的时间
	DWORD dwLastread;					//数据库更新的时间
	_tag_heart()
	{
		//socketaddr      = 0;
		dwCurTime		= 0;
		dwLastread		= 0;
		s_ip = "";
	}
}tag_heart;
typedef map<unsigned int,tag_heart> mapfilter_heart;
// CIOCPModel类
class CIOCPModel
{
public:
	CIOCPModel(void);
	~CIOCPModel(void);

public:

	// 启动服务器
	bool Start();

	//	停止服务器
	void Stop();

	// 加载Socket库
	bool LoadSocketLib();

	// 卸载Socket库，彻底完事
	void UnloadSocketLib() { WSACleanup(); }

	// 获得本机的IP地址
	CString GetLocalIP();

	// 设置监听端口
	void SetPort( const int& nPort ) { m_nPort=nPort; }

	// 设置主界面的指针，用于调用显示信息到界面中
	void SetMainDlg( CDialog* p ) { m_pMain=p; }

	public:

	// 初始化IOCP
	bool _InitializeIOCP();

	// 初始化Socket
	bool _InitializeListenSocket();

	// 最后释放资源
	void _DeInitialize();

	// 投递Accept请求
	bool _PostAccept( PER_IO_CONTEXT* pAcceptIoContext ); 

	// 投递接收数据请求
	bool _PostRecv( PER_IO_CONTEXT* pIoContext );

	// 在有客户端连入的时候，进行处理
	bool _DoAccpet( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );

	// 发送数据
	bool _DoSend( PER_IO_CONTEXT* pSendIoContext );

	// 在有接收的数据到达的时候，进行处理
	bool _DoRecv( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );

	// 将客户端的相关信息存储到数组中
	void _AddToContextList( PER_SOCKET_CONTEXT *pSocketContext );

	// 将客户端的信息从数组中移除
	void _RemoveContext( PER_SOCKET_CONTEXT *pSocketContext );

	// 清空客户端信息
	void _ClearContextList();

	// 将句柄绑定到完成端口中
	bool _AssociateWithIOCP( PER_SOCKET_CONTEXT *pContext);

	// 处理完成端口上的错误
	bool HandleError( PER_SOCKET_CONTEXT *pContext,const DWORD& dwErr );

	// 线程函数，为IOCP请求服务的工作者线程
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);

	// 获得本机的处理器数量
	int _GetNoOfProcessors();

	// 判断客户端Socket是否已经断开
	bool _IsSocketAlive(SOCKET s);

	// 在主界面中显示信息
	void _ShowMessage( const CString szFormat,...) const;

	// 解析数据
	int _GetRecordList(unsigned char *pRspPkt, unsigned char *bReaderAddr,unsigned char *bReaderType, Record_list *pRecordList);

	// 校验和
	BYTE _Check_Sum(const BYTE * buf, int len);

	//获取本地时间
	void _GetCurrenttime(char *currtime);

	//识别信息转换json格式
	void _RecognitionToJson(unsigned char *reader, unsigned char *wristband, unsigned char *time, char *JsonString, unsigned char *type);

	//识别信息转换json格式
	void _RecognitionToJson(int scan_rssi, unsigned char *wristband, unsigned char *time, char *JsonString, unsigned char *type);

	//获取服务器时间转换json格式
	void _GetSeverTimeTOJson(int devicetype, unsigned char *device, unsigned char *time, char *JsonString);

	//天喻协议将数据转为字符串
	void _HttpPostStr(unsigned int ireaderID,Record_t* record,unsigned char *type,unsigned char *inouttype,unsigned char *outbuf = NULL);

	//字符串中移除字符
	void removeAll(string &str,char ch);

	//ascii转hex
	void Ascii2Hex(unsigned char *hexBuf, const char *ascBuf, unsigned char hexbufLen);

	//时间比较函数
	bool _CompareTime();

	//时间比较函数
	bool GetCurHourByMin(time_t curTm, int BegHour, int BegMin, int EndHour, int EndMin);

	//
	string ToHex(unsigned long iNum);
	//BCD
	int  DectoBCD(int Dec, unsigned char *Bcd, int len) ;

private:

	HANDLE                       m_hShutdownEvent;              // 用来通知线程系统退出的事件，为了能够更好的退出线程

	HANDLE                       m_hIOCompletionPort;           // 完成端口的句柄

	HANDLE*                      m_phWorkerThreads;             // 工作者线程的句柄指针

	int		                     m_nThreads;                    // 生成的线程数量

	CString                      m_strIP;                       // 服务器端的IP地址


	CDialog*                     m_pMain;                       // 主界面的界面指针，用于在主界面中显示消息

	CRITICAL_SECTION             m_csContextList;               // 用于Worker线程同步的互斥量

	CArray<PER_SOCKET_CONTEXT*>  m_arrayClientContext;          // 客户端Socket的Context信息        

	PER_SOCKET_CONTEXT*          m_pListenContext;              // 用于监听的Socket的Context信息

	LPFN_ACCEPTEX                m_lpfnAcceptEx;                // AcceptEx 和 GetAcceptExSockaddrs 的函数指针，用于调用这两个扩展函数
	LPFN_GETACCEPTEXSOCKADDRS    m_lpfnGetAcceptExSockAddrs; 

public:
	int                          m_nPort;                       // 服务器端的监听端口
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
	//_RecordsetPtr m_pRecordset;//记录集对象指针，用来执行SQL语句并记录查询结果	
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

	// 返回秒
	time_t StringToDatetime(char *str);

	//获取本地时间
	void _SQLformatTime(char *currtime);
	// 线程函数，为IOCP请求服务的工作者线程
	static DWORD WINAPI _ListenThread(LPVOID lpParam);
    // 处理线程函数，为IOCP处理数据
	static DWORD WINAPI _ProcessThread(LPVOID lpParam);
     //处理线程
	void _Process_data();
	//处理线程（只处理一包内容)
	void _Process_data(unsigned char *iocpbuf,unsigned char* mqbuf);
	//发送消息队列
	void _SendMsmq(CString labelmsg,CString msg);
	//发送消息队列
	void _SendMsmq(CString labelmsg,CString msg,unsigned char* msgbuf);

private:
	CRITICAL_SECTION             m_csSqlList;               // 用于sqlserver访问的互斥量
	HANDLE						 hThread_heart;
	DEFTOOLS::LogFile a;
	
    std::list<unsigned char*> processList;           //处理线程队列
	bool                  g_bExtiThread;             //全局退出变量
	CRITICAL_SECTION             m_csProcess;        //处理线程锁
	//HANDLE						 hThread_process;//处理线程句柄
	HANDLE hThread[DEFAULT_ASQL_THREAD_COUNT];
	agsMSMQ mque; //消息队列
};

